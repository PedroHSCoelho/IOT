-- =====================================================================
-- testes_regras.sql — testes de aceite do banco (seção 12 do BANCO_DE_DADOS.md)
-- =====================================================================
-- COMO RODAR: cole o arquivo inteiro no SQL Editor do Supabase (com o banco já
-- criado e com 01 a 05 aplicados) e execute. A última linha mostra uma tabela
-- com cada teste, o resultado ESPERADO, o OBTIDO e OK = true/false.
--
-- NADA FICA GRAVADO: tudo roda dentro de uma transação que termina em ROLLBACK.
-- Cada teste cria seu próprio funcionário temporário (badges 9101, 9102...), para
-- um teste não atrapalhar o outro nem os dados reais (101, 102, 103).
-- Os testes usam sempre "hoje às 00:01 (Brasília)" como horário-base, então
-- funcionam em qualquer hora do dia.
--
-- T10 (persistência) é MANUAL — ver o fim do arquivo.
-- =====================================================================

BEGIN;

CREATE TEMP TABLE t_res (ord SERIAL, id TEXT, descricao TEXT, esperado TEXT, obtido TEXT, ok BOOLEAN);

-- Ajudante: registra o resultado de um teste.
CREATE FUNCTION pg_temp.chk(p_id TEXT, p_desc TEXT, p_esperado TEXT, p_obtido TEXT) RETURNS void
LANGUAGE sql AS $$
    INSERT INTO t_res (id, descricao, esperado, obtido, ok)
    VALUES (p_id, p_desc, p_esperado, p_obtido, p_esperado IS NOT DISTINCT FROM p_obtido);
$$;


-- ---------- T1, T2, T3: alternância e duplicata ----------
DO $$
DECLARE
    base TIMESTAMPTZ := (dia_local(now())::TIMESTAMP) AT TIME ZONE 'America/Sao_Paulo';  -- hoje 00:00 SP
    r RECORD;
BEGIN
    INSERT INTO funcionario (nome, badge_id) VALUES ('T-alternancia', 9101);

    SELECT * INTO r FROM registrar_ponto(9101, 'botao', 1, base + interval '1 min');
    PERFORM pg_temp.chk('T1', 'Botão, primeiro do dia', 'CHEGADA/true', r.tipo || '/' || r.contabilizado);

    SELECT * INTO r FROM registrar_ponto(9101, 'botao', 2, base + interval '2 min');
    PERFORM pg_temp.chk('T2', 'Botão de novo, seq seguinte', 'SAIDA/true', r.tipo || '/' || r.contabilizado);

    SELECT * INTO r FROM registrar_ponto(9101, 'botao', 2, base + interval '2 min');
    PERFORM pg_temp.chk('T3', 'Repete T2 (mesmo seq): motivo DUPLICADO', 'DUPLICADO', r.motivo);
    PERFORM pg_temp.chk('T3b', 'Duplicata não gravou linha nova', '2',
        (SELECT count(*) FROM evento WHERE badge_id = 9101)::TEXT);
END $$;


-- ---------- T4: botão (CHEGADA) + BLE PRESENTE 2 min depois ----------
DO $$
DECLARE
    base TIMESTAMPTZ := (dia_local(now())::TIMESTAMP) AT TIME ZONE 'America/Sao_Paulo';
    r RECORD;
BEGIN
    INSERT INTO funcionario (nome, badge_id) VALUES ('T-mesmo-tipo', 9104);
    PERFORM registrar_ponto(9104, 'botao', 1, base + interval '1 min');
    SELECT * INTO r FROM registrar_ponto(9104, 'ble', 1, base + interval '3 min', 'PRESENTE');
    PERFORM pg_temp.chk('T4', 'BLE PRESENTE logo após CHEGADA: não contabiliza',
        'CHEGADA/false/MESMO_TIPO_DO_ANTERIOR', r.tipo || '/' || r.contabilizado || '/' || r.motivo);
END $$;


-- ---------- T5, T6: crachá desconhecido e inativo ----------
DO $$
DECLARE
    base TIMESTAMPTZ := (dia_local(now())::TIMESTAMP) AT TIME ZONE 'America/Sao_Paulo';
    r RECORD;
BEGIN
    SELECT * INTO r FROM registrar_ponto(999, 'botao', 1, base + interval '1 min', NULL, NULL, NULL, false, '{"t":"T5"}');
    PERFORM pg_temp.chk('T5', 'Badge 999 (não cadastrado)', 'CRACHA_DESCONHECIDO/sem evento',
        r.motivo || '/' || CASE WHEN r.evento_id IS NULL THEN 'sem evento' ELSE 'COM evento' END);
    PERFORM pg_temp.chk('T5b', 'Linha em evento_rejeitado', '1',
        (SELECT count(*) FROM evento_rejeitado WHERE motivo = 'CRACHA_DESCONHECIDO' AND payload @> '{"t":"T5"}')::TEXT);

    SELECT * INTO r FROM registrar_ponto(103, 'botao', 1, base + interval '1 min', NULL, NULL, NULL, false, '{"t":"T6"}');
    PERFORM pg_temp.chk('T6', 'Badge 103 (inativo)', 'FUNCIONARIO_INATIVO/sem evento',
        r.motivo || '/' || CASE WHEN r.evento_id IS NULL THEN 'sem evento' ELSE 'COM evento' END);
    PERFORM pg_temp.chk('T6b', 'Linha em evento_rejeitado', '1',
        (SELECT count(*) FROM evento_rejeitado WHERE motivo = 'FUNCIONARIO_INATIVO' AND payload @> '{"t":"T6"}')::TEXT);
    PERFORM pg_temp.chk('T6c', 'Nada entrou em evento para o badge 103', '0',
        (SELECT count(*) FROM evento WHERE badge_id = 103)::TEXT);
END $$;


-- ---------- T7: hora desregulada (R5) ----------
DO $$
DECLARE
    r RECORD;
    ts_gravado TIMESTAMPTZ;
BEGIN
    INSERT INTO funcionario (nome, badge_id) VALUES ('T-hora', 9107);

    -- 3 dias de atraso, fila = false: corrigida.
    SELECT * INTO r FROM registrar_ponto(9107, 'botao', 1, now() - interval '3 days', NULL, NULL, NULL, false);
    SELECT e.ts_evento INTO ts_gravado FROM evento e WHERE e.id = r.evento_id;
    PERFORM pg_temp.chk('T7', '3 dias de atraso, fila=false: HORA_CORRIGIDA', 'HORA_CORRIGIDA/true',
        r.motivo || '/' || (ts_gravado = now()));

    -- 1 hora no futuro: corrigida.
    SELECT * INTO r FROM registrar_ponto(9107, 'botao', 2, now() + interval '1 hour', NULL, NULL, NULL, false);
    PERFORM pg_temp.chk('T7b', '1 h no futuro: HORA_CORRIGIDA', 'HORA_CORRIGIDA', r.motivo);

    -- 3 dias de atraso, mas fila = true: ATRASO ESPERADO, hora preservada.
    SELECT * INTO r FROM registrar_ponto(9107, 'botao', 3, now() - interval '3 days', NULL, NULL, NULL, true);
    SELECT e.ts_evento INTO ts_gravado FROM evento e WHERE e.id = r.evento_id;
    PERFORM pg_temp.chk('T7c', '3 dias de atraso, fila=true: hora preservada', 'sem motivo/true',
        COALESCE(r.motivo, 'sem motivo') || '/' || (ts_gravado = now() - interval '3 days'));
END $$;


-- ---------- T8: dia completo = 480 min, saldo 0 ----------
DO $$
DECLARE
    dia2 TIMESTAMPTZ := ((dia_local(now()) - 2)::TIMESTAMP) AT TIME ZONE 'America/Sao_Paulo';  -- anteontem 00:00 SP
    v RECORD;
BEGIN
    INSERT INTO funcionario (nome, badge_id) VALUES ('T-jornada', 9108);
    -- fila = true porque são pontos "antigos" (se não, R5 corrigiria a hora).
    PERFORM registrar_ponto(9108, 'botao', 1, dia2 + interval '8 hours',  NULL, NULL, NULL, true);
    PERFORM registrar_ponto(9108, 'botao', 2, dia2 + interval '12 hours', NULL, NULL, NULL, true);
    PERFORM registrar_ponto(9108, 'botao', 3, dia2 + interval '13 hours', NULL, NULL, NULL, true);
    PERFORM registrar_ponto(9108, 'botao', 4, dia2 + interval '17 hours', NULL, NULL, NULL, true);

    SELECT j.* INTO v FROM vw_jornada_diaria j
      JOIN funcionario f ON f.id = j.funcionario_id WHERE f.badge_id = 9108;
    PERFORM pg_temp.chk('T8', 'Jornada 08-12 e 13-17: 480 min, saldo 0, sem pendência',
        '480/0/false', v.minutos_trabalhados || '/' || v.saldo_min || '/' || v.saida_pendente);

    -- Semanal: este dia entra na semana (segunda-feira) correspondente.
    PERFORM pg_temp.chk('T8b', 'vw_jornada_semanal soma os 480 min da semana', '480',
        (SELECT s.minutos_trabalhados FROM vw_jornada_semanal s
           JOIN funcionario f ON f.id = s.funcionario_id
          WHERE f.badge_id = 9108 AND s.semana = date_trunc('week', (dia_local(now()) - 2)::TIMESTAMP)::DATE)::TEXT);
END $$;


-- ---------- T9: chegada hoje sem saída ----------
DO $$
DECLARE
    base TIMESTAMPTZ := (dia_local(now())::TIMESTAMP) AT TIME ZONE 'America/Sao_Paulo';
    v RECORD;
BEGIN
    INSERT INTO funcionario (nome, badge_id) VALUES ('T-presente', 9109);
    PERFORM registrar_ponto(9109, 'botao', 1, base + interval '1 min');

    SELECT p.* INTO v FROM vw_presenca_atual p JOIN funcionario f ON f.id = p.funcionario_id WHERE f.badge_id = 9109;
    PERFORM pg_temp.chk('T9', 'vw_presenca_atual: presente', 'true', v.presente::TEXT);

    SELECT j.* INTO v FROM vw_jornada_diaria j JOIN funcionario f ON f.id = j.funcionario_id WHERE f.badge_id = 9109;
    PERFORM pg_temp.chk('T9b', 'Jornada de hoje conta até agora (> 0) e não é pendência',
        'true/false', (v.minutos_trabalhados > 0) || '/' || v.saida_pendente);

    -- Saída registrada => deixa de estar presente.
    PERFORM registrar_ponto(9109, 'botao', 2, base + interval '2 min');
    SELECT p.* INTO v FROM vw_presenca_atual p JOIN funcionario f ON f.id = p.funcionario_id WHERE f.badge_id = 9109;
    PERFORM pg_temp.chk('T9c', 'Após a SAIDA: não presente', 'false', v.presente::TEXT);
END $$;


-- ---------- Extras: regras não cobertas por T1-T9 ----------
DO $$
DECLARE
    base TIMESTAMPTZ := (dia_local(now())::TIMESTAMP) AT TIME ZONE 'America/Sao_Paulo';
    dia3 TIMESTAMPTZ := ((dia_local(now()) - 3)::TIMESTAMP) AT TIME ZONE 'America/Sao_Paulo';
    r RECORD;
    v RECORD;
BEGIN
    -- E1: payload inválido.
    SELECT * INTO r FROM registrar_ponto(101, 'wifi', 1, base + interval '1 min');
    PERFORM pg_temp.chk('E1', 'Origem inválida', 'PAYLOAD_INVALIDO', r.motivo);
    SELECT * INTO r FROM registrar_ponto(101, 'ble', 1, base + interval '1 min', 'TALVEZ');
    PERFORM pg_temp.chk('E1b', 'BLE com estado inválido', 'PAYLOAD_INVALIDO', r.motivo);
    SELECT * INTO r FROM registrar_ponto(NULL, 'botao', 1, base + interval '1 min');
    PERFORM pg_temp.chk('E1c', 'Badge nulo', 'PAYLOAD_INVALIDO', r.motivo);

    -- E2: BLE AUSENTE como primeiro do dia não vira saída contabilizada.
    INSERT INTO funcionario (nome, badge_id) VALUES ('T-saida-orfa', 9201);
    SELECT * INTO r FROM registrar_ponto(9201, 'ble', 1, base + interval '1 min', 'AUSENTE');
    PERFORM pg_temp.chk('E2', 'AUSENTE sem chegada anterior: não contabiliza',
        'SAIDA/false/SAIDA_SEM_CHEGADA', r.tipo || '/' || r.contabilizado || '/' || r.motivo);

    -- E3: BLE segue a alternância normal (PRESENTE, AUSENTE) e conta horas.
    INSERT INTO funcionario (nome, badge_id) VALUES ('T-ble', 9202);
    PERFORM registrar_ponto(9202, 'ble', 1, base + interval '1 min', 'PRESENTE');
    SELECT * INTO r FROM registrar_ponto(9202, 'ble', 2, base + interval '2 min', 'AUSENTE');
    PERFORM pg_temp.chk('E3', 'BLE PRESENTE depois AUSENTE', 'SAIDA/true', r.tipo || '/' || r.contabilizado);

    -- E4: saída pendente em dia anterior não conta horas.
    INSERT INTO funcionario (nome, badge_id) VALUES ('T-pendente', 9203);
    PERFORM registrar_ponto(9203, 'botao', 1, dia3 + interval '8 hours', NULL, NULL, NULL, true);
    SELECT j.* INTO v FROM vw_jornada_diaria j JOIN funcionario f ON f.id = j.funcionario_id WHERE f.badge_id = 9203;
    PERFORM pg_temp.chk('E4', 'Dia anterior sem saída: 0 min e saida_pendente',
        '0/true', v.minutos_trabalhados || '/' || v.saida_pendente);

    -- E5: R8 — contato do crachá e do PC (BLE) é atualizado.
    UPDATE dispositivo SET ultimo_contato = NULL WHERE identificador IN ('101', 'pc-lab-01');
    PERFORM registrar_ponto(101, 'ble', 777, base + interval '1 min', 'PRESENTE');
    PERFORM pg_temp.chk('E5', 'R8: ultimo_contato do crachá e do PC preenchidos', '2',
        (SELECT count(*) FROM dispositivo WHERE identificador IN ('101', 'pc-lab-01') AND ultimo_contato IS NOT NULL)::TEXT);
    PERFORM pg_temp.chk('E5b', 'vw_dispositivos mostra o PC como online', 'true',
        (SELECT online FROM vw_dispositivos WHERE identificador = 'pc-lab-01')::TEXT);
    PERFORM pg_temp.chk('E5c', 'Gateway (sem contato) aparece offline', 'false',
        (SELECT online FROM vw_dispositivos WHERE identificador = 'gw-lab')::TEXT);

    -- E6: vw_historico entrega a hora no fuso de Brasília.
    PERFORM pg_temp.chk('E6', 'vw_historico: 08:00 de Brasília aparece como 08:00', '08:00',
        (SELECT to_char(ts_evento, 'HH24:MI') FROM vw_historico
          WHERE badge_id = 101 AND seq = -1));

    -- E7: seed — o dia de ontem do badge 101 soma 510 min (8 h 30 min).
    PERFORM pg_temp.chk('E7', 'Seed: ontem do badge 101 = 510 min', '510',
        (SELECT minutos_trabalhados FROM vw_jornada_diaria j JOIN funcionario f ON f.id = j.funcionario_id
          WHERE f.badge_id = 101 AND j.dia = dia_local(now()) - 1)::TEXT);
END $$;


-- ---------- Segurança: o que o "nodered" e a API pública NÃO podem fazer ----------
DO $$
DECLARE
    r RECORD;
    base TIMESTAMPTZ := (dia_local(now())::TIMESTAMP) AT TIME ZONE 'America/Sao_Paulo';
    resultados TEXT[] := '{}';
    n INTEGER;
BEGIN
    -- Cada tentativa proibida DEVE falhar com "permission denied" (SQLSTATE 42501).
    SET LOCAL ROLE nodered;

    BEGIN DROP TABLE evento;                          resultados := resultados || 'DROP:PERMITIDO'::TEXT;
    EXCEPTION WHEN insufficient_privilege THEN        resultados := resultados || 'DROP:negado'::TEXT; END;

    BEGIN PERFORM 1 FROM funcionario LIMIT 1;         resultados := resultados || 'SELECT_funcionario:PERMITIDO'::TEXT;
    EXCEPTION WHEN insufficient_privilege THEN        resultados := resultados || 'SELECT_funcionario:negado'::TEXT; END;

    BEGIN PERFORM 1 FROM evento LIMIT 1;              resultados := resultados || 'SELECT_evento:PERMITIDO'::TEXT;
    EXCEPTION WHEN insufficient_privilege THEN        resultados := resultados || 'SELECT_evento:negado'::TEXT; END;

    BEGIN DELETE FROM evento;                         resultados := resultados || 'DELETE_evento:PERMITIDO'::TEXT;
    EXCEPTION WHEN insufficient_privilege THEN        resultados := resultados || 'DELETE_evento:negado'::TEXT; END;

    BEGIN UPDATE funcionario SET nome = 'x';          resultados := resultados || 'UPDATE_funcionario:PERMITIDO'::TEXT;
    EXCEPTION WHEN insufficient_privilege THEN        resultados := resultados || 'UPDATE_funcionario:negado'::TEXT; END;

    -- (firmware_versao e ultimo_contato PODEM ser alterados; as demais colunas, não)
    BEGIN UPDATE dispositivo SET identificador = 'x'; resultados := resultados || 'UPDATE_identificador:PERMITIDO'::TEXT;
    EXCEPTION WHEN insufficient_privilege THEN        resultados := resultados || 'UPDATE_identificador:negado'::TEXT; END;

    -- O que DEVE funcionar:
    BEGIN SELECT * INTO r FROM registrar_ponto(102, 'botao', 5001, base + interval '1 min');
          resultados := resultados || 'registrar_ponto:ok'::TEXT;
    EXCEPTION WHEN OTHERS THEN resultados := resultados || ('registrar_ponto:ERRO ' || SQLERRM); END;

    -- TODAS as views (algumas usam dia_local(), que roda com a permissão de quem consulta).
    BEGIN PERFORM 1 FROM vw_presenca_atual;  PERFORM 1 FROM vw_jornada_diaria;
          PERFORM 1 FROM vw_jornada_semanal; PERFORM 1 FROM vw_historico;
          PERFORM 1 FROM vw_dispositivos;
          resultados := resultados || 'views:ok'::TEXT;
    EXCEPTION WHEN OTHERS THEN resultados := resultados || ('views:ERRO ' || SQLERRM); END;

    -- Atenção: com RLS, um UPDATE/INSERT bloqueado pode NÃO dar erro, só alterar 0 linhas.
    -- Por isso conferimos o número de linhas afetadas (ROW_COUNT), e não só a ausência de erro.
    BEGIN UPDATE dispositivo SET ultimo_contato = now() WHERE identificador = 'gw-lab';
          GET DIAGNOSTICS n = ROW_COUNT;
          resultados := resultados || (CASE WHEN n = 1 THEN 'UPDATE_ultimo_contato:ok' ELSE 'UPDATE_ultimo_contato:' || n || ' linhas' END);
    EXCEPTION WHEN OTHERS THEN resultados := resultados || ('UPDATE_ultimo_contato:ERRO ' || SQLERRM); END;

    BEGIN UPDATE dispositivo SET firmware_versao = '9.9' WHERE identificador = '101';
          GET DIAGNOSTICS n = ROW_COUNT;
          resultados := resultados || (CASE WHEN n = 1 THEN 'UPDATE_firmware:ok' ELSE 'UPDATE_firmware:' || n || ' linhas' END);
    EXCEPTION WHEN OTHERS THEN resultados := resultados || ('UPDATE_firmware:ERRO ' || SQLERRM); END;

    BEGIN INSERT INTO comando (dispositivo_id, comando) SELECT id, 'BIPE' FROM dispositivo WHERE identificador = '101';
          GET DIAGNOSTICS n = ROW_COUNT;
          resultados := resultados || (CASE WHEN n = 1 THEN 'INSERT_comando:ok' ELSE 'INSERT_comando:' || n || ' linhas' END);
    EXCEPTION WHEN OTHERS THEN resultados := resultados || ('INSERT_comando:ERRO ' || SQLERRM); END;

    RESET ROLE;

    -- API pública (anon e authenticated): não pode ler nada nem chamar a função.
    SET LOCAL ROLE anon;
    BEGIN PERFORM 1 FROM funcionario LIMIT 1;         resultados := resultados || 'anon_funcionario:PERMITIDO'::TEXT;
    EXCEPTION WHEN insufficient_privilege THEN        resultados := resultados || 'anon_funcionario:negado'::TEXT; END;
    BEGIN PERFORM 1 FROM vw_historico LIMIT 1;        resultados := resultados || 'anon_vw_historico:PERMITIDO'::TEXT;
    EXCEPTION WHEN insufficient_privilege THEN        resultados := resultados || 'anon_vw_historico:negado'::TEXT; END;
    BEGIN PERFORM registrar_ponto(102, 'botao', 5002, base + interval '1 min');
                                                      resultados := resultados || 'anon_registrar_ponto:PERMITIDO'::TEXT;
    EXCEPTION WHEN insufficient_privilege THEN        resultados := resultados || 'anon_registrar_ponto:negado'::TEXT; END;
    RESET ROLE;

    SET LOCAL ROLE authenticated;
    BEGIN PERFORM 1 FROM evento LIMIT 1;              resultados := resultados || 'auth_evento:PERMITIDO'::TEXT;
    EXCEPTION WHEN insufficient_privilege THEN        resultados := resultados || 'auth_evento:negado'::TEXT; END;
    RESET ROLE;

    PERFORM pg_temp.chk('S1', 'nodered: tabelas fechadas, só o necessário aberto',
        'DROP:negado|SELECT_funcionario:negado|SELECT_evento:negado|DELETE_evento:negado|UPDATE_funcionario:negado|UPDATE_identificador:negado|registrar_ponto:ok|views:ok|UPDATE_ultimo_contato:ok|UPDATE_firmware:ok|INSERT_comando:ok',
        array_to_string(resultados[1:11], '|'));
    PERFORM pg_temp.chk('S2', 'anon/authenticated: tudo negado',
        'anon_funcionario:negado|anon_vw_historico:negado|anon_registrar_ponto:negado|auth_evento:negado',
        array_to_string(resultados[12:15], '|'));
END $$;


-- ---------- Resultado ----------
SELECT id, descricao, esperado, obtido, ok FROM t_res ORDER BY ord;

ROLLBACK;  -- desfaz tudo: nenhum dado de teste permanece no banco

-- =====================================================================
-- T10 (MANUAL) — Persistência (evidência do desafio extra)
-- =====================================================================
-- 1. Rode:  SELECT count(*) FROM vw_historico;   e anote o número.
-- 2. Reinicie o Node-RED (o banco é externo a ele).
-- 3. Rode a consulta de novo: o número deve ser o mesmo (todos os dados continuam).
-- 4. Se der tempo: pause e restaure o projeto no painel do Supabase e repita.
-- =====================================================================
