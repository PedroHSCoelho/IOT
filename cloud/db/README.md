# Banco de dados — Crachá Inteligente de Ponto

Banco **PostgreSQL 17 no Supabase** (projeto `ponto-unifesp`, região São Paulo). A especificação completa está em [`../../BANCO_DE_DADOS.md`](../../BANCO_DE_DADOS.md); este arquivo é o passo a passo prático.

```text
db/
├── README.md              ← este arquivo
├── reset.sql              ← apaga tudo e recria o schema public (só desenvolvimento)
├── rodar_sql.py           ← roda um .sql no banco pelo terminal (opcional)
├── init/                  ← scripts, rodar NESTA ordem
│   ├── 01_schema.sql      ← tabelas, restrições, índices, RLS
│   ├── 02_funcoes.sql     ← dia_local() e registrar_ponto()
│   ├── 03_views.sql       ← vw_presenca_atual, vw_jornada_diaria, ...
│   ├── 04_usuarios.sql    ← usuário nodered e permissões
│   └── 05_seed.sql        ← dados de exemplo
└── testes/
    └── testes_regras.sql  ← testes de aceite (T1-T9 + extras + segurança)
```

---

## 1. Criar o banco do zero

1. No painel do Supabase, abra o projeto → **SQL Editor** → **New query**.
2. Teste a conexão: `SELECT version(), now();` (deve mostrar o PostgreSQL e a hora em UTC).
3. Cole e execute, **um arquivo por vez, nesta ordem**: `01` → `02` → `03` → `04` → `05`.
   Todos podem ser repetidos sem estragar nada (`IF NOT EXISTS`, `CREATE OR REPLACE`, `ON CONFLICT DO NOTHING`).
4. Defina a **senha do usuário `nodered`** (uma vez só, **não** coloque a senha em arquivo do Git):

   ```sql
   ALTER ROLE nodered WITH PASSWORD 'uma-senha-forte-aqui';
   ```

   Guarde a senha em `cloud/.env` (`SUPABASE_DB_PASSWORD`). Sem senha, o `nodered` não consegue conectar.

## 2. Conferir que deu certo

```sql
-- Tabelas e quantidade de linhas (esperado: 3 funcionários, 4 eventos, 3 dispositivos)
SELECT 'funcionario' t, count(*) FROM funcionario UNION ALL
SELECT 'evento', count(*) FROM evento UNION ALL
SELECT 'dispositivo', count(*) FROM dispositivo;

-- Colunas e tipos de uma tabela
SELECT column_name, data_type, is_nullable, column_default
  FROM information_schema.columns
 WHERE table_schema = 'public' AND table_name = 'evento'
 ORDER BY ordinal_position;

-- Views do painel
SELECT * FROM vw_jornada_diaria;   -- ontem do crachá 101: 510 min (8 h 30)
SELECT * FROM vw_presenca_atual;
SELECT * FROM vw_historico ORDER BY ts_evento DESC;
SELECT * FROM vw_dispositivos;
```

## 3. Registrar um ponto na mão

O Node-RED só faz esta chamada. Você pode fazer o mesmo no SQL Editor:

```sql
SELECT * FROM registrar_ponto(
  101,                 -- badge_id
  'botao',             -- origem: 'botao' ou 'ble'
  1,                   -- seq (contador do crachá)
  now()                -- hora do ponto
);
-- devolve: evento_id | tipo (CHEGADA/SAIDA) | contabilizado | motivo
```

Para BLE, informe o estado: `registrar_ponto(101, 'ble', 1, now(), 'PRESENTE')`.
Ordem completa dos parâmetros: `(badge, origem, seq, ts, estado, rssi, snr, fila, payload)`.

> Pontos de teste feitos assim **ficam gravados**. Para limpar os seus:
> `DELETE FROM evento WHERE seq > 0 AND badge_id = 101;` (o seed usa `seq` negativo e fica intacto).

## 4. Rodar os testes

Cole `testes/testes_regras.sql` inteiro no SQL Editor e execute. A última consulta mostra uma tabela com
**teste · esperado · obtido · ok**. Tudo roda dentro de uma transação com `ROLLBACK`: **nenhum dado fica gravado**.

| Teste | O que verifica |
| --- | --- |
| T1–T3 | Botão alterna CHEGADA/SAIDA (R3); pacote repetido vira `DUPLICADO` (R2) |
| T4 | BLE logo após CHEGADA fica no histórico sem contar horas (R4) |
| T5–T6 | Crachá desconhecido e funcionário inativo vão para `evento_rejeitado` (R1) |
| T7 | Relógio desregulado: `HORA_CORRIGIDA` (R5); ponto de fila preserva a hora |
| T8 | Jornada 08-12 e 13-17 = 480 min, saldo 0 (R6) |
| T9 | Chegada sem saída hoje: presente e jornada contando (R6, R7) |
| E1–E7 | Payload inválido, saída sem chegada, BLE, saída pendente, contato dos dispositivos (R8), fuso horário, seed |
| S1–S2 | `nodered` não faz `DROP`/`DELETE`/`SELECT` nas tabelas; `anon`/`authenticated` não acessam nada |
| **T10 (manual)** | **Persistência.** Anote `SELECT count(*) FROM vw_historico;`, reinicie o Node-RED e confira que o número é o mesmo. Se der tempo, pause e restaure o projeto no painel do Supabase |

Os testes usam funcionários temporários (badges 9101…) e sempre "hoje 00:01 em Brasília" como base, então funcionam em qualquer horário.

## 5. Recomeçar do zero (desenvolvimento)

1. Rode `reset.sql` (**apaga todos os dados**).
2. Rode `init/01` a `init/05` de novo.
3. O usuário `nodered` continua existindo e **mantém a senha**. As permissões voltam com o `04`.

## 6. Conexão do Node-RED

Use o **Session pooler** (botão **Connect** do painel). A conexão direta do plano gratuito usa IPv6 e costuma falhar em hotspot.

| Campo | Valor |
| --- | --- |
| Host | o que o painel mostrar em *Session pooler* (ex.: `aws-0-<região>.pooler.supabase.com`) |
| Porta | `5432` |
| Banco | `postgres` |
| Usuário | `nodered.<ref-do-projeto>` |
| SSL | ligado |

No nó `postgresql`, sempre use parâmetros (`$1`, `$2`…) em vez de montar o SQL com texto (evita *SQL injection*):

```sql
SELECT * FROM registrar_ponto($1,$2,$3,$4,$5,$6,$7,$8,$9);
```

## 7. Segurança (resumo)

- **RLS ligado** em todas as tabelas, sem políticas: a API REST pública do Supabase não enxerga nada.
- `anon` e `authenticated` perderam todo acesso a tabelas, views, sequências e funções (`04_usuarios.sql`).
- `nodered` só pode: executar `registrar_ponto()`, ler as `vw_*`, ler/criar/atualizar `comando` e atualizar `dispositivo.ultimo_contato`.
  Não consegue apagar tabelas nem ler `funcionario`/`evento` diretamente.
- `registrar_ponto()` é `SECURITY DEFINER` com `search_path` fixo; as views rodam com permissão do dono, por isso o `REVOKE` para `anon`/`authenticated` é essencial.
- **Nunca** commitar `cloud/.env`, a senha do banco, nem as chaves `anon`/`service_role`.

## 8. Decisões e limitações (para a documentação/arguição)

- **Fuso:** o Supabase roda em UTC. Todo cálculo de "dia" usa `dia_local()` (`America/Sao_Paulo` explícito).
- **Hora de fila:** pontos com `fila = true` podem ter mais de 24 h de atraso sem serem corrigidos (o atraso é esperado). O adiantamento de mais de 5 min é sempre corrigido.
- **Extensão da R4:** o primeiro evento do dia sendo uma `SAIDA` (ex.: BLE `AUSENTE` sem chegada) fica no histórico com `contabilizado = false` e motivo `SAIDA_SEM_CHEGADA`.
- **Semana de 5 dias:** `carga_semanal_min = carga_diaria_min × 5`.
- **Seed:** usa `seq` negativo (−1 a −4) para nunca colidir com o contador real do crachá.
- **Limitações aceitas:** jornada que atravessa a meia-noite não é tratada; evento de fila que chega muito atrasado "no meio" de outros não recalcula o tipo dos posteriores.
- **Plano gratuito:** o projeto pausa após ~1 semana sem uso (reativar antes dos ensaios e da apresentação de 04/12); não há backup automático (exportar um dump/CSV antes da apresentação); a demonstração precisa de internet.
