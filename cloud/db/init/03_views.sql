-- =====================================================================
-- 03_views.sql — VIEWS para o painel (regras R6 e R7)
-- =====================================================================
-- Uma VIEW é uma consulta SQL salva com nome. O Node-RED faz
--     SELECT * FROM vw_jornada_diaria;
-- como se fosse uma tabela, mas o resultado é sempre calculado na hora a partir
-- dos eventos, então nunca fica desatualizado.
--
-- Atenção (segurança): estas views rodam com as permissões do dono (postgres),
-- por isso o usuário "nodered" consegue lê-las sem acessar as tabelas. Em
-- compensação, 04_usuarios.sql tira o acesso a elas dos papéis da API pública
-- (anon/authenticated). Sem esse REVOKE, qualquer pessoa leria os dados pela internet.
--
-- Pode ser rodado várias vezes (CREATE OR REPLACE).
-- =====================================================================


-- ---------------------------------------------------------------------
-- vw_presenca_atual (R7): quem está dentro agora?
-- ---------------------------------------------------------------------
-- Presente = o último evento CONTABILIZADO de HOJE é uma CHEGADA.
-- LEFT JOIN LATERAL: para cada funcionário, busca "o último evento de hoje".
CREATE OR REPLACE VIEW vw_presenca_atual AS
SELECT
    f.id                           AS funcionario_id,
    f.nome,
    COALESCE(u.tipo = 'CHEGADA', false) AS presente,
    u.ts_evento                    AS desde,
    u.origem
FROM funcionario f
LEFT JOIN LATERAL (
    SELECT e.tipo, e.ts_evento, e.origem
      FROM evento e
     WHERE e.badge_id = f.badge_id
       AND e.contabilizado
       AND dia_local(e.ts_evento) = dia_local(now())
     ORDER BY e.ts_evento DESC, e.id DESC
     LIMIT 1
) u ON true
WHERE f.ativo;


-- ---------------------------------------------------------------------
-- vw_jornada_diaria (R6): horas trabalhadas por funcionário e dia
-- ---------------------------------------------------------------------
-- Técnica: função de janela LEAD(). Dentro de cada (crachá, dia), "olha" o evento
-- seguinte. Se um evento é CHEGADA e o próximo é SAIDA, o intervalo entre eles
-- é tempo trabalhado.
--   * CHEGADA sem SAIDA em HOJE      -> conta até agora (jornada em andamento).
--   * CHEGADA sem SAIDA em dia ANTERIOR -> não conta; saida_pendente = true.
CREATE OR REPLACE VIEW vw_jornada_diaria AS
WITH ev AS (
    SELECT
        e.badge_id,
        dia_local(e.ts_evento)       AS dia,
        e.ts_evento,
        e.tipo,
        LEAD(e.tipo)      OVER w     AS prox_tipo,
        LEAD(e.ts_evento) OVER w     AS prox_ts
    FROM evento e
    WHERE e.contabilizado
    WINDOW w AS (PARTITION BY e.badge_id, dia_local(e.ts_evento)
                 ORDER BY e.ts_evento, e.id)
),
agg AS (
    SELECT
        badge_id,
        dia,
        min(ts_evento) FILTER (WHERE tipo = 'CHEGADA') AS primeira_chegada,
        max(ts_evento) FILTER (WHERE tipo = 'SAIDA')   AS ultima_saida,
        COALESCE(sum(
            CASE
                WHEN tipo = 'CHEGADA' AND prox_tipo = 'SAIDA'
                    THEN extract(epoch FROM prox_ts - ts_evento) / 60
                WHEN tipo = 'CHEGADA' AND prox_tipo IS NULL AND dia = dia_local(now())
                    THEN extract(epoch FROM now() - ts_evento) / 60
                ELSE 0
            END), 0)::INTEGER                           AS minutos_trabalhados,
        COALESCE(bool_or(tipo = 'CHEGADA' AND prox_tipo IS NULL AND dia < dia_local(now())), false)
                                                        AS saida_pendente
    FROM ev
    GROUP BY badge_id, dia
)
SELECT
    f.id                                    AS funcionario_id,
    f.nome,
    a.dia,
    a.primeira_chegada,
    a.ultima_saida,
    a.minutos_trabalhados,
    f.carga_diaria_min,
    a.minutos_trabalhados - f.carga_diaria_min AS saldo_min,
    a.saida_pendente
FROM agg a
JOIN funcionario f ON f.badge_id = a.badge_id;


-- ---------------------------------------------------------------------
-- vw_jornada_semanal: soma por semana (segunda a domingo)
-- ---------------------------------------------------------------------
-- Suposição: semana de 5 dias úteis (carga semanal = carga diária x 5).
CREATE OR REPLACE VIEW vw_jornada_semanal AS
SELECT
    funcionario_id,
    nome,
    date_trunc('week', dia::TIMESTAMP)::DATE         AS semana,   -- segunda-feira
    sum(minutos_trabalhados)::INTEGER                AS minutos_trabalhados,
    max(carga_diaria_min) * 5                        AS carga_semanal_min,
    sum(minutos_trabalhados)::INTEGER - max(carga_diaria_min) * 5 AS saldo_min
FROM vw_jornada_diaria
GROUP BY funcionario_id, nome, date_trunc('week', dia::TIMESTAMP);


-- ---------------------------------------------------------------------
-- vw_historico: eventos com nome e hora já no horário de Brasília
-- ---------------------------------------------------------------------
-- "AT TIME ZONE" converte o instante absoluto (timestamptz) para a hora que
-- o relógio de parede marcava em São Paulo (timestamp sem fuso).
CREATE OR REPLACE VIEW vw_historico AS
SELECT
    e.id,
    f.id                                              AS funcionario_id,
    f.nome,
    e.badge_id,
    e.ts_evento AT TIME ZONE 'America/Sao_Paulo'      AS ts_evento,
    e.recebido_em AT TIME ZONE 'America/Sao_Paulo'    AS recebido_em,
    e.origem,
    e.tipo,
    e.seq,
    e.fila,
    e.rssi,
    e.snr,
    e.contabilizado,
    e.motivo
FROM evento e
JOIN funcionario f ON f.badge_id = e.badge_id;


-- ---------------------------------------------------------------------
-- vw_dispositivos: dispositivos + "online" (contato nos últimos 2 minutos)
-- ---------------------------------------------------------------------
CREATE OR REPLACE VIEW vw_dispositivos AS
SELECT
    d.id,
    d.tipo,
    d.identificador,
    d.funcionario_id,
    f.nome                                        AS funcionario_nome,
    d.descricao,
    d.firmware_versao,
    d.ultimo_contato,
    COALESCE(d.ultimo_contato > now() - interval '2 minutes', false) AS online
FROM dispositivo d
LEFT JOIN funcionario f ON f.id = d.funcionario_id;
