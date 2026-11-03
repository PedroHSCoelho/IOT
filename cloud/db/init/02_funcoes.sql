-- =====================================================================
-- 02_funcoes.sql — FUNÇÕES do banco (regras de negócio R1 a R5 e R8)
-- =====================================================================
-- Conceitos:
--   FUNÇÃO (PL/pgSQL): código guardado dentro do banco. O Node-RED faz uma
--       única chamada e o banco decide tudo (a regra fica num lugar só).
--   SECURITY DEFINER: a função roda com as permissões de QUEM A CRIOU (postgres),
--       não de quem a chama. Assim o usuário "nodered" consegue registrar pontos
--       sem ter acesso direto às tabelas.
--   SET search_path = public: impede que alguém "engane" a função criando um
--       objeto com o mesmo nome em outro schema (boa prática de segurança).
--
-- Pode ser rodado várias vezes (CREATE OR REPLACE).
-- =====================================================================


-- ---------------------------------------------------------------------
-- dia_local(): "em que dia (no Brasil) caiu este instante?"
-- ---------------------------------------------------------------------
-- O Supabase roda em UTC. Um ponto às 22h de Brasília já é "dia seguinte" em UTC.
-- Por isso TODA conversão de dia passa por esta função, que fixa America/Sao_Paulo
-- explicitamente e nunca depende da configuração do servidor.
CREATE OR REPLACE FUNCTION dia_local(p_ts timestamptz)
RETURNS date
LANGUAGE sql
IMMUTABLE
PARALLEL SAFE
SET search_path = ''
AS $$
    SELECT (p_ts AT TIME ZONE 'America/Sao_Paulo')::date;
$$;


-- ---------------------------------------------------------------------
-- registrar_ponto(): ÚNICA porta de entrada dos pontos
-- ---------------------------------------------------------------------
CREATE OR REPLACE FUNCTION registrar_ponto(
    p_badge_id  INTEGER,
    p_origem    TEXT,                       -- 'botao' | 'ble'
    p_seq       INTEGER,
    p_ts        TIMESTAMPTZ,
    p_estado    TEXT     DEFAULT NULL,      -- só BLE: 'PRESENTE' | 'AUSENTE'
    p_rssi      SMALLINT DEFAULT NULL,
    p_snr       REAL     DEFAULT NULL,
    p_fila      BOOLEAN  DEFAULT false,
    p_payload   JSONB    DEFAULT NULL
)
RETURNS TABLE (evento_id BIGINT, tipo TEXT, contabilizado BOOLEAN, motivo TEXT)
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = public
AS $$
#variable_conflict use_column
DECLARE
    v_func      funcionario%ROWTYPE;
    v_exist     evento%ROWTYPE;
    v_agora     TIMESTAMPTZ := now();       -- "recebido_em" deste ponto
    v_ts        TIMESTAMPTZ;                -- hora que será usada nos cálculos
    v_motivos   TEXT[]      := '{}';
    v_tipo      TEXT;
    v_ult_tipo  TEXT;
    v_contab    BOOLEAN     := true;
    v_id        BIGINT;
    v_registro  JSONB;                      -- o que gravamos em evento_rejeitado
BEGIN
    -- Se o chamador não mandou o JSON original, montamos um com o que temos.
    v_registro := COALESCE(p_payload, jsonb_build_object(
        'badge', p_badge_id, 'origem', p_origem, 'seq', p_seq,
        'ts', p_ts, 'estado', p_estado));

    -- -----------------------------------------------------------------
    -- Bloco 0: o pacote está completo e faz sentido? (PAYLOAD_INVALIDO)
    -- -----------------------------------------------------------------
    IF p_badge_id IS NULL OR p_seq IS NULL OR p_ts IS NULL
       OR p_origem IS NULL OR p_origem NOT IN ('botao', 'ble')
       OR (p_origem = 'ble' AND (p_estado IS NULL OR p_estado NOT IN ('PRESENTE', 'AUSENTE')))
    THEN
        INSERT INTO evento_rejeitado (motivo, payload) VALUES ('PAYLOAD_INVALIDO', v_registro);
        RETURN QUERY SELECT NULL::BIGINT, NULL::TEXT, NULL::BOOLEAN, 'PAYLOAD_INVALIDO'::TEXT;
        RETURN;
    END IF;

    -- Trava por crachá: se dois pontos do mesmo crachá chegarem ao mesmo tempo,
    -- um espera o outro terminar. Sem isso, a alternância (R3/R4) poderia errar.
    PERFORM pg_advisory_xact_lock(p_badge_id);

    -- -----------------------------------------------------------------
    -- Bloco 1 (R1): crachá existe e funcionário está ativo?
    -- -----------------------------------------------------------------
    SELECT * INTO v_func FROM funcionario WHERE badge_id = p_badge_id;

    IF NOT FOUND THEN
        INSERT INTO evento_rejeitado (motivo, payload) VALUES ('CRACHA_DESCONHECIDO', v_registro);
        RETURN QUERY SELECT NULL::BIGINT, NULL::TEXT, NULL::BOOLEAN, 'CRACHA_DESCONHECIDO'::TEXT;
        RETURN;
    ELSIF NOT v_func.ativo THEN
        INSERT INTO evento_rejeitado (motivo, payload) VALUES ('FUNCIONARIO_INATIVO', v_registro);
        RETURN QUERY SELECT NULL::BIGINT, NULL::TEXT, NULL::BOOLEAN, 'FUNCIONARIO_INATIVO'::TEXT;
        RETURN;
    END IF;

    -- -----------------------------------------------------------------
    -- Bloco 2 (R8): chegou mensagem => o dispositivo está vivo.
    -- Feito antes da checagem de duplicata: uma retransmissão também prova contato.
    -- -----------------------------------------------------------------
    UPDATE dispositivo SET ultimo_contato = v_agora
     WHERE tipo = 'cracha' AND identificador = p_badge_id::TEXT;

    IF p_origem = 'ble' THEN
        UPDATE dispositivo SET ultimo_contato = v_agora
         WHERE tipo = 'pc' AND funcionario_id = v_func.id;
    END IF;

    -- -----------------------------------------------------------------
    -- Bloco 3 (R2): já recebemos exatamente este pacote?
    -- -----------------------------------------------------------------
    SELECT * INTO v_exist FROM evento
     WHERE badge_id = p_badge_id AND origem = p_origem AND seq = p_seq;

    IF FOUND THEN
        RETURN QUERY SELECT v_exist.id, v_exist.tipo, v_exist.contabilizado, 'DUPLICADO'::TEXT;
        RETURN;
    END IF;

    -- -----------------------------------------------------------------
    -- Bloco 4 (R5): a hora do crachá é confiável?
    --   * mais de 5 min no futuro => relógio adiantado/desregulado;
    --   * mais de 24 h no passado => relógio atrasado/desregulado, EXCETO se o
    --     ponto veio da fila (fila = true): nesse caso o atraso é esperado,
    --     pois foi batido fora do alcance do gateway e enviado depois.
    -- Em ambos os casos usamos a hora de recebimento no lugar.
    -- -----------------------------------------------------------------
    IF p_ts > v_agora + interval '5 minutes'
       OR (NOT p_fila AND p_ts < v_agora - interval '24 hours')
    THEN
        v_ts := v_agora;
        v_motivos := array_append(v_motivos, 'HORA_CORRIGIDA'::TEXT);
    ELSE
        v_ts := p_ts;
    END IF;

    -- -----------------------------------------------------------------
    -- Bloco 5 (R3 e R4): CHEGADA ou SAIDA? Conta horas?
    -- Último evento contabilizado do funcionário, no mesmo dia (horário de
    -- Brasília), com hora igual ou anterior à deste evento.
    -- -----------------------------------------------------------------
    SELECT e.tipo INTO v_ult_tipo
      FROM evento e
     WHERE e.badge_id = p_badge_id
       AND e.contabilizado
       AND dia_local(e.ts_evento) = dia_local(v_ts)
       AND e.ts_evento <= v_ts
     ORDER BY e.ts_evento DESC, e.id DESC
     LIMIT 1;

    IF p_origem = 'ble' THEN
        -- O PC sabe se o crachá chegou ou saiu.
        v_tipo := CASE p_estado WHEN 'PRESENTE' THEN 'CHEGADA' ELSE 'SAIDA' END;
    ELSE
        -- O botão não sabe: o banco alterna.
        v_tipo := CASE WHEN v_ult_tipo = 'CHEGADA' THEN 'SAIDA' ELSE 'CHEGADA' END;
    END IF;

    IF v_ult_tipo = v_tipo THEN
        -- R4: dois do mesmo tipo seguidos. Fica no histórico, mas não conta horas.
        v_contab  := false;
        v_motivos := array_append(v_motivos, 'MESMO_TIPO_DO_ANTERIOR'::TEXT);
    ELSIF v_ult_tipo IS NULL AND v_tipo = 'SAIDA' THEN
        -- Extensão de R4: o primeiro do dia não pode ser uma saída (não há chegada
        -- para fechar). Sem isto o histórico teria uma SAIDA "órfã" contabilizada.
        v_contab  := false;
        v_motivos := array_append(v_motivos, 'SAIDA_SEM_CHEGADA'::TEXT);
    END IF;

    -- -----------------------------------------------------------------
    -- Bloco 6: grava o evento.
    -- ON CONFLICT DO NOTHING é a rede de segurança final da regra R2.
    -- -----------------------------------------------------------------
    INSERT INTO evento (ts_evento, recebido_em, badge_id, origem, tipo, seq, fila,
                        rssi, snr, contabilizado, motivo, payload)
    VALUES (v_ts, v_agora, p_badge_id, p_origem, v_tipo, p_seq, COALESCE(p_fila, false),
            p_rssi, p_snr, v_contab, NULLIF(array_to_string(v_motivos, ','), ''), p_payload)
    ON CONFLICT ON CONSTRAINT evento_badge_origem_seq_uq DO NOTHING
    RETURNING id INTO v_id;

    IF v_id IS NULL THEN
        SELECT * INTO v_exist FROM evento
         WHERE badge_id = p_badge_id AND origem = p_origem AND seq = p_seq;
        RETURN QUERY SELECT v_exist.id, v_exist.tipo, v_exist.contabilizado, 'DUPLICADO'::TEXT;
        RETURN;
    END IF;

    -- -----------------------------------------------------------------
    -- Bloco 7: devolve o resultado (o Node-RED usa para o painel e o log).
    -- -----------------------------------------------------------------
    RETURN QUERY SELECT v_id, v_tipo, v_contab, NULLIF(array_to_string(v_motivos, ','), '');
END;
$$;

COMMENT ON FUNCTION registrar_ponto IS
    'Única porta de entrada dos pontos. Aplica as regras R1-R5 e R8 e devolve (evento_id, tipo, contabilizado, motivo).';
