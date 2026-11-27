-- =====================================================================
-- 07_origem_lora.sql — a coluna "origem" passa a distinguir o ponto do CRACHÁ REMOTO (LoRa)
-- =====================================================================
-- Pedido de 26/11/2026 (aprovado pelo usuário): chegada/saída precisa dizer, na coluna "origem",
-- por onde veio:
--   'botao' = botão ligado ao GATEWAY (vai direto pelo Wi-Fi do gateway)
--   'lora'  = botão PRG do CRACHÁ de campo, sem internet: crachá -> LoRa -> gateway -> MQTT
--   'ble'   = PC por aproximação (implementado, mas sem uso: o PC não registra ponto)
--
-- Bônus (era um bug): a regra R2 recusa (badge, origem, seq) repetido. Crachá e botão do gateway
-- dividiam a origem 'botao' e o seq do crachá recomeça em 1, colidindo com pontos antigos (seq 1-14):
-- o banco descartava o ponto LoRa como DUPLICADO, sem erro. Com origem própria, o par é outro.
--
-- Também desfaz a coluna "canal" tentada antes no mesmo dia (redundante com "origem").
-- Pode ser rodado várias vezes. Depois: rodar 02_funcoes.sql (aceita 'lora' na validação).
-- =====================================================================

DROP VIEW IF EXISTS vw_historico;                        -- depende de "canal"
ALTER TABLE evento DROP COLUMN IF EXISTS canal;

ALTER TABLE evento DROP CONSTRAINT IF EXISTS evento_origem_check;
ALTER TABLE evento ADD  CONSTRAINT evento_origem_check CHECK (origem IN ('botao', 'ble', 'lora'));

CREATE VIEW vw_historico AS
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

REVOKE ALL ON vw_historico FROM anon, authenticated;
GRANT SELECT ON vw_historico TO nodered;
