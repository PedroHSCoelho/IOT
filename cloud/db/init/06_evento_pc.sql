-- =====================================================================
-- 06_evento_pc.sql — histórico de BLOQUEIO / DESBLOQUEIO do PC
-- =====================================================================
-- Decisão de 26/11/2026 (aprovada pelo usuário): tabela NOVA e separada de "evento".
-- Motivo: "evento" é o PONTO (R1-R8: alternância CHEGADA/SAIDA, horas trabalhadas).
-- Um bloqueio por distância NÃO é ponto (armadilha 14 do CLAUDE.md), então não entra lá.
--
-- Analogia: "evento" é o cartão de ponto; "evento_pc" é o caderninho da portaria que anota
-- "tela trancou às 10:02, destrancou às 10:05".
--
-- Cada linha = uma TRANSIÇÃO (só quando o estado muda; o "estou vivo" a cada 30 s não entra aqui).
-- Pode ser rodado várias vezes.
-- =====================================================================

CREATE TABLE IF NOT EXISTS evento_pc (
    id             BIGSERIAL   PRIMARY KEY,
    ts_evento      TIMESTAMPTZ NOT NULL,                 -- quando o agente bloqueou/liberou (hora do PC)
    recebido_em    TIMESTAMPTZ NOT NULL DEFAULT now(),   -- quando chegou ao servidor
    dispositivo_id INTEGER     NOT NULL REFERENCES dispositivo (id),   -- qual PC
    estado         TEXT        NOT NULL
                   CHECK (estado IN ('BLOQUEADO', 'DESBLOQUEADO')),
    modo           TEXT        CHECK (modo IN ('AUTO', 'FIXO')),       -- só no desbloqueio
    motivo         TEXT,                                 -- ex.: "clique do botao", "afastou-se"
    rssi           SMALLINT,                             -- sinal BLE no momento (pode ser vazio)
    payload        JSONB                                 -- mensagem original, para auditoria
);

CREATE INDEX IF NOT EXISTS evento_pc_disp_ts_idx ON evento_pc (dispositivo_id, ts_evento);

ALTER TABLE evento_pc ENABLE ROW LEVEL SECURITY;

-- O nodered só ESCREVE (menor privilégio). Para o INSERT ... SELECT achar o PC pelo nome, ele já
-- pode ler id/tipo/identificador de dispositivo (04_usuarios.sql).
GRANT INSERT ON evento_pc TO nodered;
GRANT USAGE ON SEQUENCE evento_pc_id_seq TO nodered;

DROP POLICY IF EXISTS nodered_evento_pc_insert ON evento_pc;
CREATE POLICY nodered_evento_pc_insert ON evento_pc FOR INSERT TO nodered WITH CHECK (true);

-- API pública do Supabase: fechada (igual às outras tabelas)
REVOKE ALL ON evento_pc FROM anon, authenticated;
REVOKE ALL ON SEQUENCE evento_pc_id_seq FROM anon, authenticated;


-- ---------------------------------------------------------------------
-- vw_eventos_pc: para o painel (hora já em Brasília e nome do PC/dono)
-- ---------------------------------------------------------------------
CREATE OR REPLACE VIEW vw_eventos_pc AS
SELECT
    p.id,
    p.ts_evento AT TIME ZONE 'America/Sao_Paulo' AS ts_evento,
    d.identificador                              AS pc,
    f.nome                                       AS funcionario_nome,
    p.estado,
    p.modo,
    p.motivo,
    p.rssi
FROM evento_pc p
JOIN dispositivo d ON d.id = p.dispositivo_id
LEFT JOIN funcionario f ON f.id = d.funcionario_id;

REVOKE ALL ON vw_eventos_pc FROM anon, authenticated;
GRANT SELECT ON vw_eventos_pc TO nodered;
