-- =====================================================================
-- reset.sql — APAGA TUDO e recria o schema "public" vazio
-- =====================================================================
-- !! SÓ PARA DESENVOLVIMENTO !! Apaga tabelas, views, funções e TODOS os dados.
-- Depois, rode de novo init/01 a init/05, nesta ordem.
--
-- Os "GRANT" abaixo recriam o que o Supabase espera encontrar no schema public
-- (sem eles, a API do Supabase deixa de funcionar). As permissões de verdade
-- voltam com 04_usuarios.sql.
-- O usuário "nodered" NÃO é apagado (ele pertence ao banco, não ao schema).
-- =====================================================================

DROP SCHEMA IF EXISTS public CASCADE;
CREATE SCHEMA public;

GRANT USAGE ON SCHEMA public TO postgres, anon, authenticated, service_role;
GRANT ALL   ON SCHEMA public TO postgres;
