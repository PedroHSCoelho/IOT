-- =====================================================================
-- 01_schema.sql — TABELAS do banco do Crachá Inteligente de Ponto
-- =====================================================================
-- Conceitos usados aqui:
--   PRIMARY KEY (chave primária): identifica cada linha de forma única.
--   FOREIGN KEY / REFERENCES (chave estrangeira): liga uma tabela a outra;
--       o banco recusa um valor que não exista na tabela apontada.
--   UNIQUE: proíbe dois valores iguais na coluna (ou na combinação de colunas).
--   CHECK: limita os valores aceitos (ex.: origem só 'botao' ou 'ble').
--   INDEX (índice): "índice remissivo" que acelera buscas por uma coluna.
--
-- Este script pode ser rodado mais de uma vez (IF NOT EXISTS).
-- =====================================================================


-- ---------------------------------------------------------------------
-- funcionario: quem trabalha na empresa
-- ---------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS funcionario (
    id                SERIAL      PRIMARY KEY,           -- número interno gerado pelo banco
    nome              TEXT        NOT NULL,
    matricula         TEXT        UNIQUE,                -- identificação usada pela empresa
    badge_id          INTEGER     NOT NULL UNIQUE,       -- ID gravado no firmware do crachá
    carga_diaria_min  INTEGER     NOT NULL DEFAULT 480   -- 480 min = 8 h
                      CHECK (carga_diaria_min > 0),
    ativo             BOOLEAN     NOT NULL DEFAULT true, -- desligado continua no histórico
    criado_em         TIMESTAMPTZ NOT NULL DEFAULT now()
);

COMMENT ON TABLE  funcionario          IS 'Funcionários. badge_id é o ID gravado no crachá.';
COMMENT ON COLUMN funcionario.ativo    IS 'false = não bate mais ponto, mas o histórico permanece.';


-- ---------------------------------------------------------------------
-- evento: todo ponto aceito (o histórico)
-- ---------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS evento (
    id             BIGSERIAL   PRIMARY KEY,              -- BIGSERIAL: o histórico cresce sem parar
    ts_evento      TIMESTAMPTZ NOT NULL,                 -- quando o ponto foi batido (usado nos cálculos)
    recebido_em    TIMESTAMPTZ NOT NULL DEFAULT now(),   -- quando chegou ao servidor
    badge_id       INTEGER     NOT NULL
                   REFERENCES funcionario (badge_id),    -- só aceita crachá cadastrado
    origem         TEXT        NOT NULL
                   CHECK (origem IN ('botao', 'ble')),
    tipo           TEXT        NOT NULL
                   CHECK (tipo IN ('CHEGADA', 'SAIDA')), -- decidido por registrar_ponto() (R3)
    seq            INTEGER     NOT NULL,                 -- contador, serve para descartar retransmissões
    fila           BOOLEAN     NOT NULL DEFAULT false,   -- true = batido fora do alcance e enviado depois
    rssi           SMALLINT,                             -- sinal LoRa (botão) ou BLE (PC)
    snr            REAL,                                 -- só LoRa
    contabilizado  BOOLEAN     NOT NULL DEFAULT true,    -- false = fica no histórico, não conta horas (R4)
    motivo         TEXT,                                 -- ex.: MESMO_TIPO_DO_ANTERIOR, HORA_CORRIGIDA
    payload        JSONB,                                -- mensagem original, para auditoria

    -- Se o mesmo pacote chegar duas vezes (retransmissão LoRa), o banco recusa o segundo (R2).
    CONSTRAINT evento_badge_origem_seq_uq UNIQUE (badge_id, origem, seq)
);

-- Quase toda consulta filtra por funcionário e período.
CREATE INDEX IF NOT EXISTS evento_badge_ts_idx ON evento (badge_id, ts_evento);


-- ---------------------------------------------------------------------
-- evento_rejeitado: o que chegou com problema
-- ---------------------------------------------------------------------
-- Fica separada porque um crachá desconhecido não pode entrar em "evento"
-- (a chave estrangeira exige um funcionário), mas queremos registrar a tentativa.
CREATE TABLE IF NOT EXISTS evento_rejeitado (
    id           BIGSERIAL   PRIMARY KEY,
    recebido_em  TIMESTAMPTZ NOT NULL DEFAULT now(),
    motivo       TEXT        NOT NULL
                 CHECK (motivo IN ('CRACHA_DESCONHECIDO', 'FUNCIONARIO_INATIVO', 'PAYLOAD_INVALIDO')),
    payload      JSONB
);


-- ---------------------------------------------------------------------
-- dispositivo: crachás, gateway e PCs
-- ---------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS dispositivo (
    id               SERIAL      PRIMARY KEY,
    tipo             TEXT        NOT NULL
                     CHECK (tipo IN ('cracha', 'gateway', 'pc')),
    identificador    TEXT        NOT NULL UNIQUE,        -- badge_id (crachá) ou nome/MAC (gateway, PC)
    funcionario_id   INTEGER     REFERENCES funcionario (id), -- dono; vazio no gateway
    descricao        TEXT,
    firmware_versao  TEXT,                               -- mostrada no painel (desafio OTA)
    ultimo_contato   TIMESTAMPTZ                         -- painel mostra "online" se < 2 min
);


-- ---------------------------------------------------------------------
-- comando: ordens enviadas pelo painel (apoio ao desafio OTA)
-- ---------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS comando (
    id             BIGSERIAL   PRIMARY KEY,
    dispositivo_id INTEGER     NOT NULL REFERENCES dispositivo (id),
    comando        TEXT        NOT NULL
                   CHECK (comando IN ('OTA', 'BIPE')),
    parametros     JSONB,                                -- ex.: {"url":"http://.../cracha_v2.bin","versao":"2.0"}
    status         TEXT        NOT NULL DEFAULT 'PENDENTE'
                   CHECK (status IN ('PENDENTE', 'ENVIADO', 'CONFIRMADO', 'FALHOU')),
    criado_em      TIMESTAMPTZ NOT NULL DEFAULT now(),
    enviado_em     TIMESTAMPTZ,
    confirmado_em  TIMESTAMPTZ
);


-- Índices nas chaves estrangeiras: o Postgres NÃO cria índice sozinho para FK, e
-- sem ele apagar/consultar pela tabela "pai" varre a tabela "filha" inteira.
CREATE INDEX IF NOT EXISTS comando_dispositivo_idx     ON comando (dispositivo_id);
CREATE INDEX IF NOT EXISTS dispositivo_funcionario_idx ON dispositivo (funcionario_id);


-- ---------------------------------------------------------------------
-- Segurança (específico do Supabase)
-- ---------------------------------------------------------------------
-- O Supabase expõe o schema "public" numa API REST pública. Com RLS ligado e
-- nenhuma política criada, essa API não enxerga nada. O acesso real será feito
-- pelo usuário "nodered" (04_usuarios.sql) e pela função registrar_ponto().
ALTER TABLE funcionario      ENABLE ROW LEVEL SECURITY;
ALTER TABLE evento           ENABLE ROW LEVEL SECURITY;
ALTER TABLE evento_rejeitado ENABLE ROW LEVEL SECURITY;
ALTER TABLE dispositivo      ENABLE ROW LEVEL SECURITY;
ALTER TABLE comando          ENABLE ROW LEVEL SECURITY;
