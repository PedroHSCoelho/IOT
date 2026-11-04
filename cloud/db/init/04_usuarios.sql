-- =====================================================================
-- 04_usuarios.sql — usuário "nodered" e permissões (menor privilégio)
-- =====================================================================
-- Princípio do MENOR PRIVILÉGIO: cada usuário recebe só o que precisa.
-- Se alguém descobrir a senha do Node-RED, ainda assim não consegue apagar
-- tabelas nem ler/alterar funcionários e eventos diretamente.
--
-- O QUE O "nodered" PODE FAZER:
--   * EXECUTE em registrar_ponto()            (grava os pontos)
--   * SELECT nas views vw_*                   (alimenta o painel)
--   * SELECT/INSERT/UPDATE em comando         (painel envia OTA/BIPE e lê o status)
--   * UPDATE em dispositivo.ultimo_contato    (tópicos de status do gateway e do PC)
--   * UPDATE em dispositivo.firmware_versao   (confirmação de OTA pelo crachá)
--
-- A SENHA NÃO FICA NESTE ARQUIVO (ele vai para o Git). Depois de rodar o script,
-- defina a senha UMA vez, no SQL Editor do Supabase, e guarde no cloud/.env:
--     ALTER ROLE nodered WITH PASSWORD 'uma-senha-forte-aqui';
-- Enquanto não houver senha, o usuário não consegue conectar.
--
-- Pode ser rodado várias vezes.
-- =====================================================================


-- 1) Cria o usuário (se ainda não existir). Sem SUPERUSER, CREATEDB, CREATEROLE.
DO $$
BEGIN
    IF NOT EXISTS (SELECT FROM pg_roles WHERE rolname = 'nodered') THEN
        CREATE ROLE nodered LOGIN NOSUPERUSER NOCREATEDB NOCREATEROLE NOINHERIT;
    END IF;
END
$$;

-- Limita consultas travadas: nenhuma passa de 15 s.
ALTER ROLE nodered SET statement_timeout = '15s';
ALTER ROLE nodered SET search_path = public;


-- Permite que o administrador (postgres) "vista" o papel nodered com SET ROLE.
-- Serve para TESTAR as permissões (testes_regras.sql, seção de segurança).
-- Não dá acesso novo a ninguém: o postgres já pode tudo o que o nodered pode.
GRANT nodered TO postgres WITH SET TRUE;


-- 2) Tira o acesso dos papéis da API pública do Supabase (anon e authenticated).
--    O schema "public" é exposto numa API REST; sem estes REVOKE, as views (que
--    rodam com permissão do dono) vazariam dados para qualquer pessoa na internet.
REVOKE ALL ON ALL TABLES    IN SCHEMA public FROM anon, authenticated;
REVOKE ALL ON ALL SEQUENCES IN SCHEMA public FROM anon, authenticated;
REVOKE ALL ON ALL FUNCTIONS IN SCHEMA public FROM anon, authenticated;

-- Funções nascem com EXECUTE liberado para todos ("PUBLIC"). Tiramos isso.
REVOKE ALL ON ALL FUNCTIONS IN SCHEMA public FROM PUBLIC;

-- Objetos criados NO FUTURO também nascem fechados (o Supabase, por padrão,
-- abriria tudo para anon/authenticated).
ALTER DEFAULT PRIVILEGES IN SCHEMA public REVOKE ALL ON TABLES    FROM anon, authenticated;
ALTER DEFAULT PRIVILEGES IN SCHEMA public REVOKE ALL ON SEQUENCES FROM anon, authenticated;
ALTER DEFAULT PRIVILEGES IN SCHEMA public REVOKE ALL ON FUNCTIONS FROM anon, authenticated;
ALTER DEFAULT PRIVILEGES REVOKE EXECUTE ON FUNCTIONS FROM PUBLIC;


-- 3) Concede ao "nodered" só o necessário.
GRANT USAGE ON SCHEMA public TO nodered;

GRANT EXECUTE ON FUNCTION registrar_ponto(INTEGER, TEXT, INTEGER, TIMESTAMPTZ, TEXT, SMALLINT, REAL, BOOLEAN, JSONB)
    TO nodered;

GRANT SELECT ON vw_presenca_atual, vw_jornada_diaria, vw_jornada_semanal,
                vw_historico, vw_dispositivos
    TO nodered;

-- As views rodam com a permissão do DONO para ler as tabelas, mas as FUNÇÕES chamadas
-- dentro delas rodam com a permissão de QUEM CONSULTA. Como dia_local() é usada nas views,
-- o nodered precisa poder executá-la (é uma função pura, sem acesso a dados).
GRANT EXECUTE ON FUNCTION dia_local(TIMESTAMPTZ) TO nodered;

-- comando: o painel cria (INSERT), atualiza o status (UPDATE) e precisa ler (SELECT).
GRANT SELECT, INSERT, UPDATE ON comando TO nodered;
GRANT USAGE ON SEQUENCE comando_id_seq TO nodered;

-- dispositivo: SELECT só das colunas de identificação (o UPDATE precisa achar a
-- linha com WHERE) e UPDATE só de ultimo_contato e firmware_versao (esta última é
-- atualizada quando o crachá confirma um OTA, tópico unifesp/ponto/cmd/{badge}/ack).
GRANT SELECT (id, tipo, identificador)       ON dispositivo TO nodered;
GRANT UPDATE (ultimo_contato, firmware_versao) ON dispositivo TO nodered;


-- 4) Políticas de RLS (Row Level Security) SÓ para o "nodered".
--    Com RLS ligado e nenhuma política, um usuário comum não enxerga NENHUMA linha:
--    o UPDATE roda sem erro mas altera 0 linhas (falha silenciosa!). O GRANT acima
--    libera a TABELA/COLUNA; a política libera as LINHAS. Precisa dos dois.
--    As políticas valem só "TO nodered": anon e authenticated continuam sem ver nada.
DROP POLICY IF EXISTS nodered_dispositivo_select ON dispositivo;
DROP POLICY IF EXISTS nodered_dispositivo_update ON dispositivo;
DROP POLICY IF EXISTS nodered_comando_all        ON comando;

CREATE POLICY nodered_dispositivo_select ON dispositivo FOR SELECT TO nodered USING (true);
CREATE POLICY nodered_dispositivo_update ON dispositivo FOR UPDATE TO nodered USING (true) WITH CHECK (true);
CREATE POLICY nodered_comando_all        ON comando     FOR ALL    TO nodered USING (true) WITH CHECK (true);
