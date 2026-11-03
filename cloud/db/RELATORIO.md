# Relatório — Criação do banco de dados (Crachá Inteligente de Ponto)

**Data:** 03/11/2026 · **Banco:** PostgreSQL 17.11 no Supabase (`ponto-unifesp`) · **Status:** ✅ criado, testado e documentado

## 1. Resumo

O banco foi criado do zero seguindo a ordem da seção 14 do `BANCO_DE_DADOS.md`. Existem 5 tabelas, 1 função de regra de negócio, 5 views, 1 usuário de aplicação (`nodered`) e dados de exemplo.
A suíte de testes tem **31 verificações e todas passaram** (T1–T9 da especificação, 11 extras e 2 de segurança). O teste T10 (persistência) é manual e continua pendente.

## 2. O que foi feito

| Passo | Entrega | Arquivo | Resultado |
| --- | --- | --- | --- |
| 1 | Teste de conexão | n/a | PostgreSQL 17.11 respondendo |
| 2 | Tabelas, restrições, índices, RLS | `init/01_schema.sql` | 5 tabelas, RLS ligado em todas |
| 3 | Dados de exemplo | `init/05_seed.sql` | 3 funcionários, 3 dispositivos, 4 eventos (ontem do badge 101 = 510 min) |
| 4 | Regras R1–R5 e R8 | `init/02_funcoes.sql` | `dia_local()` e `registrar_ponto()` |
| 5 | Regras R6 e R7 | `init/03_views.sql` | `vw_presenca_atual`, `vw_jornada_diaria`, `vw_jornada_semanal`, `vw_historico`, `vw_dispositivos` |
| 6 | Menor privilégio | `init/04_usuarios.sql` | usuário `nodered`, `REVOKE` de `anon`/`authenticated` |
| 7 | Testes e documentação | `testes/testes_regras.sql`, `README.md`, `reset.sql` | ver seção 3 |

Todas as mudanças foram aplicadas no Supabase como migrations (histórico visível em `list_migrations`).

## 3. Resultado dos testes

Executados no banco real, dentro de uma transação desfeita ao final (o banco voltou ao estado do seed).

| Teste | Cenário | Resultado |
| --- | --- | --- |
| T1 | Botão, primeiro do dia → CHEGADA | ✅ |
| T2 | Botão de novo → SAIDA | ✅ |
| T3 / T3b | Mesmo `seq` → `DUPLICADO`, sem linha nova | ✅ |
| T4 | BLE PRESENTE 2 min após CHEGADA → `contabilizado=false`, `MESMO_TIPO_DO_ANTERIOR` | ✅ |
| T5 / T5b | Badge 999 → `CRACHA_DESCONHECIDO` em `evento_rejeitado` | ✅ |
| T6 / T6b / T6c | Badge 103 inativo → `FUNCIONARIO_INATIVO`, nada em `evento` | ✅ |
| T7 / T7b / T7c | Hora 3 dias atrás e hora no futuro → `HORA_CORRIGIDA`; com `fila=true`, hora preservada | ✅ |
| T8 / T8b | 08-12 e 13-17 → 480 min, saldo 0; semanal = 480 | ✅ |
| T9 / T9b / T9c | Chegada sem saída hoje → presente, jornada contando; após SAIDA, não presente | ✅ |
| E1–E1c | Payload inválido (origem, estado BLE, badge nulo) | ✅ |
| E2 | BLE AUSENTE como primeiro do dia → não contabilizado | ✅ |
| E3 | BLE PRESENTE → AUSENTE conta horas | ✅ |
| E4 | Dia anterior sem saída → 0 min e `saida_pendente` | ✅ |
| E5 / E5b / E5c | R8: contato do crachá e do PC; `online` do PC; gateway offline | ✅ |
| E6 | `vw_historico` mostra 08:00 de Brasília | ✅ |
| E7 | Seed: ontem do badge 101 = 510 min | ✅ |
| S1 | `nodered` NÃO consegue `DROP`, `DELETE`, `SELECT` em tabelas nem `UPDATE` de `funcionario`/firmware. CONSEGUE `registrar_ponto`, ler views, atualizar `ultimo_contato`, inserir `comando` | ✅ |
| S2 | `anon` e `authenticated` não acessam tabelas, views nem a função | ✅ |
| **T10** | **Persistência (reiniciar Node-RED / pausar projeto)** | ⏳ **manual, pendente** |

## 4. Problemas encontrados e corrigidos

1. **Bug em `registrar_ponto()`** (achado pelos testes). `array || 'texto'` é ambíguo no Postgres e quebrava a gravação de qualquer evento com motivo (`HORA_CORRIGIDA`, `MESMO_TIPO_DO_ANTERIOR`, `SAIDA_SEM_CHEGADA`). Corrigido com `array_append(..., 'texto'::TEXT)`, no arquivo e no banco. O mesmo erro existia no arquivo de testes e também foi corrigido.
2. **Teste de permissões não conseguia assumir o papel `nodered`.** O `postgres` só tinha a opção de administrar o papel. Foi concedido `GRANT nodered TO postgres WITH SET TRUE` (registrado no `04_usuarios.sql`). Isso não dá acesso novo a ninguém.
3. **Índices de chave estrangeira** apontados pelo advisor de performance (`comando.dispositivo_id`, `dispositivo.funcionario_id`) foram criados.

## 5. Decisões que a equipe deve conhecer

- **Hora de fila (R5):** pontos com `fila=true` podem ter mais de 24 h de atraso sem correção, porque o atraso é esperado. O adiantamento de mais de 5 min é sempre corrigido. A especificação não dizia isso explicitamente.
- **Extensão da R4:** o primeiro evento do dia sendo uma SAIDA fica no histórico com `contabilizado=false` e motivo `SAIDA_SEM_CHEGADA`. Sem isso, entraria uma saída "órfã" contabilizada.
- **Semana de 5 dias:** `carga_semanal_min = carga_diaria_min × 5`.
- **Seed com `seq` negativo** (−1 a −4), para nunca colidir com o contador real do crachá 101.
- **Views rodam com permissão do dono.** É o que permite ao `nodered` lê-las sem acessar as tabelas. A segurança depende do `REVOKE` para `anon`/`authenticated` do `04_usuarios.sql`, que foi verificado no teste S2.
- O modelo de dados (tabelas e colunas) **não foi alterado**.

## 6. Pendências (precisam de uma pessoa)

1. **Definir a senha do `nodered`** (ele existe, mas sem senha, então não conecta):
   `ALTER ROLE nodered WITH PASSWORD '...';` e guardar em `cloud/.env`. A senha não foi gerada nem gravada por mim.
2. **Corrigir o `cloud/.env`.** O `rodar_sql.py` falhou com `tenant/user postgres.<ref> not found` no host `aws-0-sa-east-1.pooler.supabase.com`. O host/região ou o usuário do pooler estão diferentes do que o painel mostra (botão **Connect → Session pooler**). Confira antes do passo do Node-RED.
3. **Teste T10 (persistência)**, descrito no `README.md` (seção 4).
4. **Passo 8 da especificação:** decidir como rodar Mosquitto e Node-RED e ligar o fluxo ao banco.
5. **Antes da apresentação (04/12):** reativar o projeto se estiver pausado e exportar um dump/CSV do histórico (o plano gratuito não tem backup).

## 7. Advisors do Supabase

- **Segurança:** apenas 5 avisos informativos (`rls_enabled_no_policy`), que são esperados e intencionais: RLS ligado sem políticas significa que a API pública não vê nada.
- **Performance:** 2 avisos de índice em chave estrangeira, já corrigidos.

## 8. Atualização: Node-RED + broker MQTT (03/11/2026, à noite)

**Instalado em `cloud/servidor/`** (sem administrador): Node-RED 5.0.9, paleta `node-red-contrib-postgresql`, broker MQTT `aedes` (no lugar do Mosquitto, que no Windows só tem instalador) e cliente `mqtt` para testes.

**Verificado ponta a ponta:** crachá simulado → broker → Node-RED → `registrar_ponto()` no Supabase. O 102 gerou uma `CHEGADA` contabilizada, o 999 foi para `evento_rejeitado`, e as mensagens de status do gateway e do PC os deixaram `online`. Os dados de teste foram apagados depois.

**Correções feitas durante a integração:**
1. **`.env` com o host errado** (`sa-east-1`; o projeto está em `us-east-1`). Corrigido e completadas as variáveis `SUPABASE_DB_*`.
2. **TLS:** o Node não reconhece a autoridade do certificado do Supabase. Resolvido **com verificação**, usando o `prod-ca-2021.crt` baixado do painel (sem desligar a checagem do certificado).
3. **Falha silenciosa de RLS (erro meu):** `dispositivo` e `comando` tinham RLS sem política, então o `nodered` não via nenhuma linha. O `UPDATE ultimo_contato` rodava sem erro mas alterava 0 linhas, e o `INSERT` em `comando` não inseria nada. O teste S1 passou por engano porque só checava "sem erro". Corrigido com políticas **só para o `nodered`** (`04_usuarios.sql`, seção 4); o teste agora confere `ROW_COUNT = 1`. A API pública (`anon`/`authenticated`) continua sem ver nada (S2 ✅).
4. **Contagem:** a suíte tem **31** checks (18 T + 11 extras + 2 de segurança), e não 32 como escrito antes. Todos passam.

**Pendências restantes:** T10 (persistência), painel/dashboard do Node-RED (ainda não montado), agente BLE do PC e firmware/gateway, dump do histórico antes de 04/12.

## 9. Atualização: painel, senha do editor e pendências (03/11/2026, noite)

**Entregue:** painel de gerência (Dashboard 2.0) em `http://127.0.0.1:1880/dashboard`, com presença, jornada do dia, semana (tabela + gráfico), histórico, dispositivos, comandos BIPE/OTA e exportação CSV. Verificado no navegador com dados reais; o BIPE gravou um comando na tabela `comando` e os dois CSV responderam 200. Senha no editor do Node-RED (usuário `admin`, hash em `.env`): sem login a API responde 401, com senha errada 403, com a certa 200.

**Outro erro meu achado e corrigido:** ao abrir o painel, o log mostrou `permission denied for function dia_local`. As funções chamadas dentro de uma view rodam com a permissão de quem consulta, e eu tinha retirado o `EXECUTE` de `dia_local()` de todos. O teste de segurança só lia `vw_historico` (que não usa a função). Corrigido com `GRANT EXECUTE ... TO nodered` e o teste agora lê **as 5 views**. Suíte: 31 de 31.

**PDFs lidos.** Divergências (lista de materiais desatualizada, regra de 30 min × alternância, tabela `jornada` × views, aedes × Mosquitto) e todas as pendências do projeto inteiro estão em `PENDENCIAS.md` (raiz). Convenções e armadilhas para quem retomar: `CLAUDE.md`.

**Decisão do usuário:** a mesma senha para o admin do banco, o `nodered` e o editor do Node-RED (risco conhecido e aceito).
