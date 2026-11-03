-- =====================================================================
-- 05_seed.sql — dados de exemplo (para o painel já abrir com conteúdo)
-- =====================================================================
-- Só nomes fictícios. Pode ser rodado várias vezes: ON CONFLICT DO NOTHING
-- ignora o que já existe.
-- =====================================================================

-- Funcionários: 101 = crachá real da demonstração; 102 = só para testes (6 h);
-- 103 = inativo (serve para testar a regra R1).
INSERT INTO funcionario (nome, matricula, badge_id, carga_diaria_min, ativo) VALUES
    ('Funcionário Teste 1',           'MAT-0001', 101, 480, true),
    ('Funcionário Teste 2',           'MAT-0002', 102, 360, true),
    ('Funcionário Teste 3 (inativo)', 'MAT-0003', 103, 480, false)
ON CONFLICT (badge_id) DO NOTHING;

-- Dispositivos: o crachá 101, o gateway do laboratório e o PC do funcionário 101.
INSERT INTO dispositivo (tipo, identificador, funcionario_id, descricao, firmware_versao)
SELECT 'cracha', '101', f.id, 'Crachá do Funcionário Teste 1 (Heltec WiFi LoRa 32 V3)', '1.0'
  FROM funcionario f WHERE f.badge_id = 101
ON CONFLICT (identificador) DO NOTHING;

INSERT INTO dispositivo (tipo, identificador, funcionario_id, descricao, firmware_versao)
VALUES ('gateway', 'gw-lab', NULL, 'Gateway LoRa do laboratório', '1.0')
ON CONFLICT (identificador) DO NOTHING;

INSERT INTO dispositivo (tipo, identificador, funcionario_id, descricao)
SELECT 'pc', 'pc-lab-01', f.id, 'PC do laboratório (Windows 11) com agente BLE'
  FROM funcionario f WHERE f.badge_id = 101
ON CONFLICT (identificador) DO NOTHING;

-- Um dia passado completo para o badge 101 (ontem, horário de Brasília):
-- chegada 08:00, saída 12:00, chegada 13:00, saída 17:30  => 8 h 30 min.
-- Os "seq" são NEGATIVOS de propósito: o crachá real começa em 1 e nunca colide
-- com estes (o par badge/origem/seq é único).
INSERT INTO evento (ts_evento, recebido_em, badge_id, origem, tipo, seq, contabilizado, motivo)
SELECT ((dia_local(now()) - 1) + v.hora) AT TIME ZONE 'America/Sao_Paulo',
       ((dia_local(now()) - 1) + v.hora) AT TIME ZONE 'America/Sao_Paulo',
       101, 'botao', v.tipo, v.seq, true, 'SEED'
  FROM (VALUES (TIME '08:00', 'CHEGADA', -1),
               (TIME '12:00', 'SAIDA',   -2),
               (TIME '13:00', 'CHEGADA', -3),
               (TIME '17:30', 'SAIDA',   -4)) AS v(hora, tipo, seq)
ON CONFLICT ON CONSTRAINT evento_badge_origem_seq_uq DO NOTHING;
