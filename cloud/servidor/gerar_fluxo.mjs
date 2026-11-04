// =====================================================================
// gerar_fluxo.mjs — gera o arquivo do fluxo do Node-RED (nodered-dados/flows.json)
// =====================================================================
// Um "fluxo" do Node-RED é um arquivo JSON com as caixinhas e as ligações.
// Em vez de escrever esse JSON na mão, escrevemos aqui o código de cada caixinha
// de forma legível e o script monta o arquivo.
//
// Rodar:  npm run gerar-fluxo     (depois reinicie o Node-RED)
//
// IMPORTANTE: este fluxo NÃO contém senha. Host, usuário e senha do banco são
// lidos das variáveis do arquivo cloud/.env quando o Node-RED inicia.
//
// ATENÇÃO ao editar: dentro dos textos de código (entre crases) NÃO use
// crase nem ${...}; use aspas e concatenação com +.
// =====================================================================
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs'

const Z = 'tab_ponto'
const FUSO = "'America/Sao_Paulo'"

// =====================================================================
// PARTE A — Código das caixinhas "function"
// =====================================================================

// ---- A1. Pontos (MQTT -> banco) -------------------------------------

// Recebe o texto da mensagem MQTT e monta os parâmetros da chamada ao banco.
const montaParametros = `// A mensagem chega como TEXTO. Tentamos transformar em objeto JSON.
let p = null;
try { p = JSON.parse(msg.payload.toString()); } catch (e) { p = null; }

if (p === null || typeof p !== 'object') {
    // Texto que não é JSON: mandamos tudo nulo; o banco registra PAYLOAD_INVALIDO.
    msg.params = [null, null, null, null, null, null, null, false,
                  JSON.stringify({ texto_recebido: String(msg.payload).slice(0, 500) })];
} else {
    // Ordem EXATA dos parâmetros de registrar_ponto():
    // (badge, origem, seq, ts, estado, rssi, snr, fila, payload)
    const rssi = p.rssi_lora ?? p.rssi_ble ?? null;
    msg.params = [p.badge ?? null, p.origem ?? null, p.seq ?? null, p.ts ?? null,
                  p.estado ?? null, rssi, p.snr ?? null, p.fila ?? false,
                  JSON.stringify(p)];
}

// $1, $2... são "parâmetros": o banco trata cada valor como DADO, nunca como comando.
// Isso impede SQL injection. NUNCA monte o SQL juntando texto com a mensagem.
msg.query = "SELECT * FROM registrar_ponto($1::integer, $2::text, $3::integer, " +
            "$4::timestamptz, $5::text, $6::smallint, $7::real, $8::boolean, $9::jsonb)";
return msg;`

// Lê a resposta de registrar_ponto() e mostra um resumo.
const formataResposta = `const r = Array.isArray(msg.payload) ? msg.payload[0] : msg.payload;
msg.payload = r;
const aceito = r.evento_id !== null;
const resumo = aceito
    ? r.tipo + (r.contabilizado ? ' (conta horas)' : ' (NAO conta horas)') + (r.motivo ? ' - ' + r.motivo : '')
    : 'REJEITADO - ' + r.motivo;
node.status({ fill: aceito ? 'green' : 'yellow', shape: 'dot', text: resumo });
node.log('registrar_ponto => ' + resumo);
return msg;`

// ---- A2. Status dos dispositivos ------------------------------------

// Mensagens de "estou vivo" do gateway e do PC atualizam dispositivo.ultimo_contato.
// Mensagens "offline" (inclusive o Last Will do gateway) NAO contam como contato.
const montaStatus = `const texto = msg.payload.toString().trim();
let obj = null;
try { obj = JSON.parse(texto); } catch (e) { /* texto simples, ex.: online */ }

// O gateway pode avisar que caiu ("offline"), em texto ou JSON.
const offline = /^offline$/i.test(texto) ||
                (obj && (obj.online === false || /^offline$/i.test(String(obj.status || obj.estado || ''))));
const ehGateway = msg.topic.includes('/gateway/');

// O estado do gateway (online/offline + qualidade do LoRa) fica na MEMÓRIA do fluxo, só para o
// painel mostrar. Não vai para o banco. Se o Node-RED reiniciar, a mensagem "retained" que o
// broker guardou devolve esse estado assim que o fluxo se inscreve de novo.
if (ehGateway) {
    const o = obj && typeof obj === 'object' ? obj : {};
    const num = (v) => (typeof v === 'number' && isFinite(v) ? v : null);
    flow.set('gw', {
        estado: offline ? 'offline' : 'online',
        rssi: num(o.rssi ?? o.rssi_lora),
        snr: num(o.snr),
        quando: Date.now(),
    });
}

if (offline) {
    // "offline" (inclusive o Last Will) NÃO conta como contato: não mexe em ultimo_contato.
    // Só manda atualizar o painel (2a saída).
    node.status({ fill: 'grey', shape: 'ring', text: 'offline (só painel)' });
    return [null, ehGateway ? { payload: null } : null];
}

let id = obj && typeof obj === 'object' ? (obj.id ?? null) : null;
let tipo;
if (ehGateway) { tipo = 'gateway'; id = id ?? 'gw-lab'; }
else           { tipo = 'pc';      id = id ?? 'pc-lab-01'; }

msg.params = [tipo, id];
msg.query = "UPDATE dispositivo SET ultimo_contato = now() WHERE tipo = $1 AND identificador = $2";
return [msg, null];`

// ---- A3. Painel: formatação das tabelas -----------------------------

// Começa uma mensagem nova e limpa (senão a "query" do passo anterior vazaria
// para as consultas do painel).
const limpaMensagem = `return { payload: null };`

// Filtros do painel: o campo "Dia" e o campo "Nome" guardam o valor na memória do fluxo e
// pedem uma atualização. O painel (a cada 10 s e após cada ponto) lê esses valores.
const guardaFiltroDia = `flow.set('f_dia', String(msg.payload || '')); return { payload: null };`
const guardaFiltroNome = `flow.set('f_nome', String(msg.payload || '')); return { payload: null };`

// Monta os parâmetros das consultas filtradas ($1 = dia, $2 = nome) e o texto "Mostrando: ...".
const montaFiltros = `let dia = String(flow.get('f_dia') || '').trim();
if (!/^\\d{4}-\\d{2}-\\d{2}$/.test(dia)) dia = '';          // só aceita AAAA-MM-DD; senão = hoje
const nome = String(flow.get('f_nome') || '').trim().slice(0, 60);

let texto = 'Mostrando: ' + (dia ? dia.split('-').reverse().join('/') : 'hoje');
if (nome) texto += ' · funcionários com "' + nome + '" no nome';
return [{ payload: null, params: [dia, nome] }, { payload: texto }];`

const ajudantes = `const hm = (m) => Math.floor(Math.abs(m) / 60) + 'h ' + String(Math.abs(m) % 60).padStart(2, '0') + 'min';
const saldo = (m) => (m < 0 ? '-' : '+') + hm(m);
const origemTxt = (o) => o === 'botao' ? 'Botão (LoRa)' : o === 'ble' ? 'Automático (BLE)' : '-';
`

const formataPresenca = ajudantes + `const rows = msg.payload || [];
const tabela = rows.map((r) => ({
    'Funcionário': r.nome,
    'Situação': r.presente ? '🟢 Presente' : '⚪ Ausente',
    'Desde': r.desde_txt || '-',
    'Origem': origemTxt(r.origem),
}));
const n = rows.filter((r) => r.presente).length;
return [
    { payload: tabela },
    { payload: n + ' presente(s) agora, de ' + rows.length + ' funcionário(s) ativo(s)' },
];`

const formataHoje = ajudantes + `const rows = msg.payload || [];
return { payload: rows.map((r) => ({
    'Funcionário': r.nome,
    'Chegada': r.primeira || '-',
    'Saída': r.ultima || '-',
    'Trabalhado': hm(r.minutos_trabalhados),
    'Carga': r.carga_diaria_min === null ? '-' : hm(r.carga_diaria_min),
    'Saldo': r.saldo_min === null ? '-' : saldo(r.saldo_min),
    // Atenção: nome de coluna SEM ponto no fim ("Obs." não aparecia: a tabela lê o ponto como caminho).
    'Observação': r.saida_pendente ? 'saída não registrada' : (r.primeira ? '' : 'sem ponto'),
})) };`

const formataSemana = ajudantes + `const rows = msg.payload || [];
const tabela = rows.map((r) => ({
    'Funcionário': r.nome,
    'Trabalhado': hm(r.minutos_trabalhados),
    'Carga semanal': r.carga_semanal_min === null ? '-' : hm(r.carga_semanal_min),
    'Saldo': r.saldo_min === null ? '-' : saldo(r.saldo_min),
}));
// Pontos do gráfico de barras: 2 séries (trabalhado x carga), em horas.
const h = (m) => Math.round((m ?? 0) / 6) / 10;
const grafico = [];
rows.forEach((r) => {
    grafico.push({ serie: 'Trabalhado (h)', nome: r.nome, horas: h(r.minutos_trabalhados) });
    grafico.push({ serie: 'Carga semanal (h)', nome: r.nome, horas: h(r.carga_semanal_min) });
});
return [{ payload: tabela }, { payload: grafico }];`

const formataHistorico = `const rows = msg.payload || [];
return { payload: rows.map((r) => ({
    'Quando': r.quando,
    'Funcionário': r.nome,
    'Tipo': r.tipo,
    'Origem': r.origem === 'botao' ? 'Botão' : 'BLE',
    'Conta horas': r.contabilizado ? 'sim' : 'não',
    'Motivo': r.motivo || '',
    'Sinal': r.rssi !== null && r.rssi !== undefined ? r.rssi + ' dBm' : '',
    'Fila': r.fila ? 'sim' : '',
})) };`

const formataDispositivos = `const rows = msg.payload || [];
const tipos = { cracha: 'Crachá', gateway: 'Gateway', pc: 'PC' };
const tabela = rows.map((r) => ({
    'Tipo': tipos[r.tipo] || r.tipo,
    'Identificador': r.identificador,
    'Dono': r.funcionario_nome || '-',
    'Firmware': r.firmware_versao || '-',
    'Último contato': r.ultimo_contato || 'nunca',
    'Status': r.online ? '🟢 online' : '🔴 offline',
}));

// Linha de texto com o aviso do gateway (online/offline) e a qualidade do LoRa.
const gw = flow.get('gw');
let txt;
if (!gw) {
    txt = 'Gateway: nenhum aviso recebido ainda.';
} else {
    const seg = Math.max(0, Math.round((Date.now() - gw.quando) / 1000));
    const ha = seg < 60 ? seg + ' s' : Math.floor(seg / 60) + ' min';
    txt = 'Gateway: ' + (gw.estado === 'online' ? '🟢 online' : '🔴 offline (avisou que caiu)') +
          (gw.rssi !== null ? ' · RSSI ' + gw.rssi + ' dBm' : '') +
          (gw.snr !== null ? ' · SNR ' + gw.snr + ' dB' : '') +
          ' · último aviso há ' + ha;
}
return [{ payload: tabela }, { payload: txt }];`

// ---- A4. Painel: comandos para o crachá (BIPE / OTA) -----------------

// Guarda os campos de texto do OTA na memória do fluxo.
const guardaOtaUrl = `flow.set('ota_url', String(msg.payload || '').trim()); return null;`
const guardaOtaVersao = `flow.set('ota_versao', String(msg.payload || '').trim()); return null;`

const montaComando = `const BADGE = '101';              // crachá da demonstração
const cmd = msg.payload;          // 'BIPE' ou 'OTA' (vem do botão)
let parametros = {};
if (cmd === 'OTA') {
    const url = flow.get('ota_url');
    const versao = flow.get('ota_versao');
    if (!url || !versao) {
        node.status({ fill: 'red', shape: 'ring', text: 'faltam URL e versão' });
        return [null, { payload: 'Preencha a URL do .bin e a versão antes de pedir o OTA.' }];
    }
    parametros = { url: url, versao: versao };
}
msg.badge = BADGE;
msg.comando = cmd;
msg.parametros = parametros;
// Registra o comando na tabela "comando" e devolve o id gerado.
msg.params = [BADGE, cmd, JSON.stringify(parametros)];
msg.query = "INSERT INTO comando (dispositivo_id, comando, parametros, status, enviado_em) " +
            "SELECT id, $2::text, $3::jsonb, 'ENVIADO', now() FROM dispositivo " +
            "WHERE tipo = 'cracha' AND identificador = $1::text RETURNING id";
return [msg, null];`

const publicaComando = `const r = (msg.payload || [])[0];
if (!r) {
    return [null, { payload: 'Dispositivo do crachá ' + msg.badge + ' não encontrado na tabela dispositivo.' }];
}
const corpo = Object.assign({ id: Number(r.id), comando: msg.comando }, msg.parametros);
return [
    { topic: 'unifesp/ponto/cmd/' + msg.badge, payload: JSON.stringify(corpo), qos: 1, retain: false },
    { payload: msg.comando + ' enviado ao crachá ' + msg.badge + ' (comando #' + r.id + ') às ' +
               new Date().toLocaleTimeString('pt-BR') + '. Aguardando a confirmação (ACK) do crachá.' },
];`

// ---- A4b. Confirmação (ACK) do crachá --------------------------------
// O crachá responde em  unifesp/ponto/cmd/{badge}/ack  com  {"id":12,"ok":true,"versao":"2.0"}
//   id     = número do comando (o mesmo que o painel enviou)
//   ok     = true se executou, false se falhou
//   versao = (opcional) nova versão do firmware, usada após um OTA bem-sucedido
const montaAck = `// O badge vem do próprio tópico: unifesp/ponto/cmd/101/ack
const partes = String(msg.topic).split('/');
const badge = partes[3];
let p = null;
try { p = JSON.parse(msg.payload.toString()); } catch (e) { p = null; }

const id = p && Number.isInteger(p.id) && p.id > 0 ? p.id : null;
if (!/^[0-9]+$/.test(badge || '') || id === null || typeof p.ok !== 'boolean') {
    node.status({ fill: 'yellow', shape: 'ring', text: 'ACK inválido (ignorado)' });
    node.warn('ACK inválido em ' + msg.topic + ': ' + String(msg.payload).slice(0, 200));
    return null;
}
const versao = typeof p.versao === 'string' && p.versao.length > 0 && p.versao.length <= 32 ? p.versao : null;

msg.badge = badge;
msg.ackId = id;
msg.ackOk = p.ok;
msg.params = [id, badge, p.ok, versao];
// Tudo numa consulta só (os $1..$4 são dados, nunca texto de SQL):
//  1) "c" confirma o comando, mas só se ele ainda estiver ENVIADO e for MESMO daquele crachá
//     (um ACK repetido ou de outro crachá não muda nada);
//  2) "d_fw" (OTA bem-sucedido com versão) grava firmware_versao e renova ultimo_contato;
//     "d_ok" (todos os outros casos) só renova ultimo_contato. São linhas DISJUNTAS (o Postgres
//     não aceita alterar a mesma linha duas vezes numa consulta) e nenhuma LÊ firmware_versao,
//     pois o nodered só tem SELECT em id, tipo e identificador (menor privilégio).
const ehOtaOk = "c.comando = 'OTA' AND $3::boolean AND $4::text IS NOT NULL";
msg.query = "WITH c AS (" +
    "UPDATE comando SET status = CASE WHEN $3::boolean THEN 'CONFIRMADO' ELSE 'FALHOU' END, " +
    "confirmado_em = now() " +
    "WHERE id = $1::bigint AND status = 'ENVIADO' " +
    "AND dispositivo_id = (SELECT id FROM dispositivo WHERE tipo = 'cracha' AND identificador = $2::text) " +
    "RETURNING dispositivo_id, comando), " +
    "d_fw AS (UPDATE dispositivo SET ultimo_contato = now(), firmware_versao = $4::text " +
    "FROM c WHERE dispositivo.id = c.dispositivo_id AND " + ehOtaOk + " RETURNING 1), " +
    "d_ok AS (UPDATE dispositivo SET ultimo_contato = now() " +
    "FROM c WHERE dispositivo.id = c.dispositivo_id AND NOT (" + ehOtaOk + ") RETURNING 1) " +
    "SELECT (SELECT count(*) FROM c) AS atualizados";
return msg;`

const resumeAck = `const r = (msg.payload || [])[0];
const n = r ? Number(r.atualizados) : 0;
let texto;
if (n === 1) {
    texto = 'Crachá ' + msg.badge + ' ' + (msg.ackOk ? 'CONFIRMOU' : 'FALHOU em') + ' o comando #' + msg.ackId +
            ' às ' + new Date().toLocaleTimeString('pt-BR') + '.';
} else {
    // ACK repetido, de comando que não existe ou de outro crachá: não altera nada.
    texto = 'ACK ignorado (comando #' + msg.ackId + ' não está aguardando confirmação do crachá ' + msg.badge + ').';
}
node.status({ fill: n === 1 ? 'green' : 'yellow', shape: 'dot', text: texto });
return { payload: texto };`

// ---- A5. Exportação CSV ----------------------------------------------

const montaCsv = `const rows = msg.payload || [];
const esc = (v) => {
    if (v === null || v === undefined) return '';
    const s = String(v);
    return /[";\\r\\n]/.test(s) ? '"' + s.replace(/"/g, '""') + '"' : s;
};
let csv = '';
if (rows.length) {
    const cols = Object.keys(rows[0]);
    csv = [cols.join(';')].concat(rows.map((r) => cols.map((c) => esc(r[c])).join(';'))).join('\\r\\n');
}
// BOM no início: o Excel reconhece os acentos. Separador ";" (padrão do Excel em português).
msg.payload = '\\ufeff' + csv;
const nome = msg.req.path.split('/').pop();
msg.headers = {
    'Content-Type': 'text/csv; charset=utf-8',
    'Content-Disposition': 'attachment; filename="' + nome + '"',
};
msg.statusCode = 200;
return msg;`

// =====================================================================
// PARTE B — Consultas SQL do painel (o usuário nodered só lê as views)
// =====================================================================
const HORA = (col, fmt) => "to_char(" + col + " AT TIME ZONE " + FUSO + ", '" + fmt + "')"

// Pedaços reutilizados nas consultas filtradas do painel ($1 = dia, $2 = nome).
const DIA = "COALESCE(NULLIF($1::text, '')::date, (now() AT TIME ZONE " + FUSO + ")::date)"
const FILTRO_NOME = "strpos(lower(p.nome), lower($2::text)) > 0"
const CARGA_OUTRO_DIA = "(SELECT max(x.carga_diaria_min) FROM vw_jornada_diaria x " +
                        "WHERE x.funcionario_id = p.funcionario_id)"
const CARGA_DIA = "COALESCE(j.carga_diaria_min, " + CARGA_OUTRO_DIA + ")"   // só na consulta com alias j

const SQL = {
  presenca:
    "SELECT nome, presente, " + HORA('desde', 'HH24:MI') + " AS desde_txt, origem " +
    "FROM vw_presenca_atual ORDER BY presente DESC, nome",
  // Consultas FILTRADAS: $1 = dia ('' = hoje) e $2 = parte do nome ('' = todos).
  // Partem de vw_presenca_atual (todos os funcionários ativos) com LEFT JOIN, para quem
  // não bateu ponto aparecer com 0 min. A carga de quem nunca teve evento vem de outro dia
  // do mesmo funcionário (o nodered não lê a tabela funcionario); se nunca teve, fica null.
  hoje:
    "SELECT p.nome, " + HORA('j.primeira_chegada', 'HH24:MI') + " AS primeira, " +
    HORA('j.ultima_saida', 'HH24:MI') + " AS ultima, " +
    "COALESCE(j.minutos_trabalhados, 0) AS minutos_trabalhados, " +
    CARGA_DIA + " AS carga_diaria_min, " +
    "COALESCE(j.saldo_min, -(" + CARGA_DIA + ")) AS saldo_min, " +
    "COALESCE(j.saida_pendente, false) AS saida_pendente " +
    "FROM vw_presenca_atual p LEFT JOIN vw_jornada_diaria j " +
    "ON j.funcionario_id = p.funcionario_id AND j.dia = " + DIA + " " +
    "WHERE " + FILTRO_NOME + " ORDER BY p.nome",
  semana:
    "SELECT p.nome, COALESCE(s.minutos_trabalhados, 0) AS minutos_trabalhados, " +
    "COALESCE(s.carga_semanal_min, " + CARGA_OUTRO_DIA + " * 5) AS carga_semanal_min, " +
    "COALESCE(s.saldo_min, -(" + CARGA_OUTRO_DIA + " * 5)) AS saldo_min " +
    "FROM vw_presenca_atual p LEFT JOIN vw_jornada_semanal s " +
    "ON s.funcionario_id = p.funcionario_id AND s.semana = date_trunc('week', (" + DIA + ")::timestamp)::date " +
    "WHERE " + FILTRO_NOME + " ORDER BY p.nome",
  historico:
    "SELECT to_char(ts_evento, 'DD/MM HH24:MI:SS') AS quando, nome, origem, tipo, contabilizado, " +
    "motivo, rssi, fila FROM vw_historico " +
    "WHERE (NULLIF($1::text, '') IS NULL OR ts_evento::date = NULLIF($1::text, '')::date) " +
    "AND strpos(lower(nome), lower($2::text)) > 0 " +
    "ORDER BY ts_evento DESC, id DESC LIMIT 50",
  dispositivos:
    "SELECT tipo, identificador, funcionario_nome, firmware_versao, " +
    HORA('ultimo_contato', 'DD/MM HH24:MI:SS') + " AS ultimo_contato, online " +
    "FROM vw_dispositivos ORDER BY tipo, identificador",
  csvHistorico:
    "SELECT to_char(ts_evento, 'YYYY-MM-DD HH24:MI:SS') AS data_hora, nome, badge_id, origem, tipo, " +
    "contabilizado, motivo, fila, rssi, snr FROM vw_historico ORDER BY ts_evento, id",
  csvJornada:
    "SELECT nome, to_char(dia, 'YYYY-MM-DD') AS dia, " + HORA('primeira_chegada', 'HH24:MI') +
    " AS primeira_chegada, " + HORA('ultima_saida', 'HH24:MI') + " AS ultima_saida, " +
    "minutos_trabalhados, carga_diaria_min, saldo_min, saida_pendente " +
    "FROM vw_jornada_diaria ORDER BY dia, nome",
}

// =====================================================================
// PARTE C — Montagem das caixinhas
// =====================================================================
const fn = (id, name, func, x, y, wires, outputs = 1) => ({
  id, type: 'function', z: Z, name, func, outputs, timeout: 0, noerr: 0,
  initialize: '', finalize: '', libs: [], x, y, wires,
})
const pg = (id, name, query, x, y, wires) => ({
  id, type: 'postgresql', z: Z, name, query, postgreSQLConfig: 'pg_supabase',
  split: false, rowsPerMsg: 1, outputs: 1, x, y, wires,
})
const comment = (id, name, y) => ({ id, type: 'comment', z: Z, name, info: '', x: 260, y, wires: [] })

// Dashboard 2.0 -------------------------------------------------------
const G = (id, name, order, width) => ({
  id, type: 'ui-group', name, page: 'ui_page', width: String(width), height: '1', order,
  showTitle: true, className: '', visible: 'true', disabled: 'false', groupType: 'default',
})
const tabela = (id, grupo, label, order, x, y) => ({
  id, type: 'ui-table', z: Z, group: grupo, name: label, label, order, width: 0, height: 0,
  maxrows: 0, passthru: false, autocols: true, showSearch: false, deselect: true,
  selectionType: 'none', columns: [], mobileBreakpoint: 'sm', mobileBreakpointType: 'defaults',
  action: 'replace', className: '', x, y, wires: [],
})
const texto = (id, grupo, order, x, y, fontSize = 18) => ({
  id, type: 'ui-text', z: Z, group: grupo, order, width: 0, height: 0, name: '', label: '',
  format: '{{msg.payload}}', layout: 'row-left', style: true, font: '', fontSize,
  color: '#1f2937', wrapText: true, className: '', value: 'payload', valueType: 'msg',
  x, y, wires: [],
})
const botao = (id, grupo, label, payload, order, x, y, wires, cor) => ({
  id, type: 'ui-button', z: Z, group: grupo, name: label, label, order, width: 0, height: 0,
  emulateClick: false, tooltip: '', color: '', bgcolor: '', className: '', icon: '',
  iconPosition: 'left', payload, payloadType: 'str', topic: 'topic', topicType: 'msg',
  buttonColor: cor, textColor: '#ffffff', iconColor: '', enableClick: true,
  enablePointerdown: false, pointerdownPayload: '', pointerdownPayloadType: 'str',
  enablePointerup: false, pointerupPayload: '', pointerupPayloadType: 'str', x, y, wires,
})
const campo = (id, grupo, label, order, x, y, wires, modo = 'text') => ({
  id, type: 'ui-text-input', z: Z, group: grupo, name: label, label, order, width: 0, height: 0,
  topicType: 'msg', mode: modo, tooltip: '', delay: 300, passthru: false, sendOnDelay: false,
  sendOnBlur: true, sendOnEnter: true, className: '', clearable: false, sendOnClear: false,
  icon: '', iconPosition: 'left', payload: '', payloadType: 'str', topic: 'topic', x, y, wires,
})

const fluxo = [
  { id: Z, type: 'tab', label: 'Ponto', disabled: false,
    info: 'Recebe os pontos pelo MQTT, grava no Supabase chamando registrar_ponto() e mostra o painel de gerência.' },

  // ---------- Configurações (reaproveitadas) ----------
  { id: 'broker_local', type: 'mqtt-broker', name: 'Broker local (aedes)',
    broker: 'localhost', port: '1883', clientid: 'nodered-ponto', autoConnect: true,
    usetls: false, protocolVersion: '4', keepalive: '60', cleansession: true,
    autoUnsubscribe: true, birthTopic: '', closeTopic: '', willTopic: '', sessionExpiry: '' },

  { id: 'pg_supabase', type: 'postgreSQLConfig', name: 'Supabase (usuario nodered)',
    host: 'SUPABASE_DB_HOST', hostFieldType: 'env',
    port: 'SUPABASE_DB_PORT', portFieldType: 'env',
    database: 'SUPABASE_DB_NAME', databaseFieldType: 'env',
    // SSL com VERIFICAÇÃO: o Node confere se o servidor é mesmo o Supabase, usando o
    // certificado raiz do projeto (prod-ca-2021.crt, público, baixado do painel em
    // Database Settings > SSL). Sem isto, o Node recusa a conexão.
    ssl: JSON.stringify({ ca: readFileSync('./prod-ca-2021.crt', 'utf8'), rejectUnauthorized: true }),
    sslFieldType: 'json',
    userFieldType: 'env', userEnv: 'SUPABASE_DB_USER',
    passwordFieldType: 'env', passwordEnv: 'SUPABASE_DB_PASSWORD',
    applicationName: 'nodered-ponto', applicationNameType: 'str',
    max: '4', maxFieldType: 'num', idle: '30000', idleFieldType: 'num',
    connectionTimeout: '10000', connectionTimeoutFieldType: 'num' },

  // ---------- Fluxo 1: pontos ----------
  comment('c1', '1) PONTOS: MQTT -> registrar_ponto() no Supabase', 40),
  { id: 'mqtt_evento', type: 'mqtt in', z: Z, name: 'unifesp/ponto/evento', topic: 'unifesp/ponto/evento',
    qos: '1', datatype: 'utf8', broker: 'broker_local', nl: false, rap: true, rh: 0, inputs: 0,
    x: 160, y: 100, wires: [['f_monta']] },
  fn('f_monta', 'monta parâmetros', montaParametros, 400, 100, [['pg_registrar']]),
  pg('pg_registrar', 'registrar_ponto()', 'SELECT 1', 620, 100, [['f_formata']]),
  // depois de cada ponto: mostra o resultado e atualiza o painel
  fn('f_formata', 'resume resposta', formataResposta, 830, 100, [['dbg_resp', 'delay_painel']]),
  { id: 'dbg_resp', type: 'debug', z: Z, name: 'resposta do banco', active: true, tosidebar: true,
    console: true, tostatus: false, complete: 'payload', targetType: 'msg', x: 1060, y: 60, wires: [] },

  // ---------- Fluxo 2: "estou vivo" dos dispositivos ----------
  comment('c2', '2) STATUS: gateway e PC atualizam "ultimo_contato" (mensagens offline são ignoradas)', 200),
  { id: 'mqtt_gw', type: 'mqtt in', z: Z, name: 'gateway/status', topic: 'unifesp/ponto/gateway/status',
    qos: '1', datatype: 'utf8', broker: 'broker_local', nl: false, rap: true, rh: 0, inputs: 0,
    x: 150, y: 260, wires: [['f_status']] },
  { id: 'mqtt_pc', type: 'mqtt in', z: Z, name: 'pc/+/status', topic: 'unifesp/ponto/pc/+/status',
    qos: '1', datatype: 'utf8', broker: 'broker_local', nl: false, rap: true, rh: 0, inputs: 0,
    x: 140, y: 320, wires: [['f_status']] },
  fn('f_status', 'monta UPDATE', montaStatus, 400, 290, [['pg_status'], ['delay_painel']], 2),
  pg('pg_status', 'atualiza ultimo_contato', 'SELECT 1', 640, 290, [['delay_painel']]),

  // ---------- Fluxo 3: erros ----------
  comment('c3', '3) ERROS: banco fora, internet caiu, senha errada...', 400),
  { id: 'catch_all', type: 'catch', z: Z, name: 'qualquer erro', scope: null, uncaught: false,
    x: 140, y: 460, wires: [['dbg_erro']] },
  { id: 'dbg_erro', type: 'debug', z: Z, name: 'ERRO', active: true, tosidebar: true,
    console: true, tostatus: false, complete: 'error.message', targetType: 'msg', x: 370, y: 460, wires: [] },

  // ---------- Fluxo 4: PAINEL (atualização) ----------
  comment('c4', '4) PAINEL: a cada 10 s e logo após cada ponto, consulta as views e atualiza a tela', 540),
  { id: 'inj_painel', type: 'inject', z: Z, name: 'a cada 10 s', props: [{ p: 'payload' }],
    repeat: '10', crontab: '', once: true, onceDelay: '2', topic: '', payload: '', payloadType: 'date',
    x: 140, y: 600, wires: [['delay_painel']] },
  // Limita a 1 atualização a cada 2 s (descarta as extras), para não sobrecarregar o banco.
  { id: 'delay_painel', type: 'delay', z: Z, name: 'no máx. 1 a cada 2 s', pauseType: 'rate',
    timeout: '5', timeoutUnits: 'seconds', rate: '1', nbRateUnits: '2', rateUnits: 'second',
    randomFirst: '1', randomLast: '5', randomUnits: 'seconds', drop: true, allowrate: false,
    outputs: 1, x: 380, y: 600, wires: [['f_limpa']] },
  fn('f_limpa', 'mensagem limpa', limpaMensagem, 600, 600,
     [['pg_presenca', 'pg_disp', 'f_filtros']]),
  // Só as 3 consultas filtradas recebem msg.params (as outras não usam $1/$2).
  fn('f_filtros', 'monta filtros (dia, nome)', montaFiltros, 600, 680,
     [['pg_hoje', 'pg_semana', 'pg_historico'], ['x_filtro']], 2),
  // Campos do painel: Dia e Nome (guardam o valor e pedem atualização imediata).
  campo('in_f_dia', 'g_filtro', 'Dia (vazio = hoje)', 1, 160, 640, [['f_f_dia']], 'date'),
  campo('in_f_nome', 'g_filtro', 'Nome contém...', 2, 160, 700, [['f_f_nome']]),
  fn('f_f_dia', 'guarda dia', guardaFiltroDia, 380, 640, [['delay_painel']]),
  fn('f_f_nome', 'guarda nome', guardaFiltroNome, 380, 700, [['delay_painel']]),

  pg('pg_presenca', 'vw_presenca_atual', SQL.presenca, 820, 560, [['f_presenca']]),
  fn('f_presenca', 'formata presença', formataPresenca, 1040, 560, [['t_presenca'], ['x_presentes']], 2),
  pg('pg_hoje', 'vw_jornada_diaria (hoje)', SQL.hoje, 820, 620, [['f_hoje']]),
  fn('f_hoje', 'formata jornada', formataHoje, 1040, 620, [['t_hoje']]),
  pg('pg_semana', 'vw_jornada_semanal', SQL.semana, 820, 680, [['f_semana']]),
  fn('f_semana', 'formata semana', formataSemana, 1040, 680, [['t_semana'], ['g_semana']], 2),
  pg('pg_historico', 'vw_historico', SQL.historico, 820, 740, [['f_hist']]),
  fn('f_hist', 'formata histórico', formataHistorico, 1040, 740, [['t_hist']]),
  pg('pg_disp', 'vw_dispositivos', SQL.dispositivos, 820, 800, [['f_disp']]),
  fn('f_disp', 'formata dispositivos', formataDispositivos, 1040, 800, [['t_disp'], ['x_gw']], 2),

  // ---------- Fluxo 5: PAINEL (comandos) ----------
  comment('c5', '5) COMANDOS: botões do painel -> tabela "comando" + MQTT unifesp/ponto/cmd/{badge}', 880),
  campo('in_ota_url', 'g_cmd', 'URL do .bin (OTA)', 3, 160, 940, [['f_ota_url']]),
  campo('in_ota_ver', 'g_cmd', 'Versão (OTA)', 4, 160, 1000, [['f_ota_ver']]),
  fn('f_ota_url', 'guarda URL', guardaOtaUrl, 400, 940, [[]]),
  fn('f_ota_ver', 'guarda versão', guardaOtaVersao, 400, 1000, [[]]),
  botao('b_bipe', 'g_cmd', 'BIPE (testar o crachá 101)', 'BIPE', 1, 160, 1060, [['f_cmd']], '#0d74ce'),
  botao('b_ota', 'g_cmd', 'ATUALIZAR FIRMWARE (OTA)', 'OTA', 2, 160, 1120, [['f_cmd']], '#b45309'),
  fn('f_cmd', 'monta comando', montaComando, 400, 1090, [['pg_cmd'], ['x_cmd']], 2),
  pg('pg_cmd', 'INSERT comando', 'SELECT 1', 620, 1060, [['f_pub']]),
  fn('f_pub', 'prepara MQTT', publicaComando, 820, 1060, [['mqtt_cmd'], ['x_cmd']], 2),
  { id: 'mqtt_cmd', type: 'mqtt out', z: Z, name: 'unifesp/ponto/cmd/{badge}', topic: '', qos: '1',
    retain: 'false', respTopic: '', contentType: '', userProps: '', correl: '', expiry: '',
    broker: 'broker_local', x: 1060, y: 1040, wires: [] },

  // ACK: o crachá responde ao comando; o status vira CONFIRMADO/FALHOU e (OTA) a versão é gravada.
  { id: 'mqtt_ack', type: 'mqtt in', z: Z, name: 'cmd/+/ack', topic: 'unifesp/ponto/cmd/+/ack',
    qos: '1', datatype: 'utf8', broker: 'broker_local', nl: false, rap: true, rh: 0, inputs: 0,
    x: 140, y: 1150, wires: [['f_ack']] },
  fn('f_ack', 'monta UPDATE do ACK', montaAck, 400, 1150, [['pg_ack']]),
  pg('pg_ack', 'confirma comando', 'SELECT 1', 620, 1150, [['f_ack_fim']]),
  fn('f_ack_fim', 'resume ACK', resumeAck, 820, 1150, [['x_cmd', 'delay_painel']]),

  // ---------- Fluxo 6: exportação CSV ----------
  comment('c6', '6) EXPORTAR CSV (espelho de ponto): http://127.0.0.1:1880/exportar/historico.csv e jornada.csv', 1180),
  { id: 'http_hist', type: 'http in', z: Z, name: '', url: '/exportar/historico.csv', method: 'get',
    upload: false, swaggerDoc: '', x: 170, y: 1240, wires: [['pg_csv_hist']] },
  pg('pg_csv_hist', 'vw_historico (tudo)', SQL.csvHistorico, 400, 1240, [['f_csv']]),
  { id: 'http_jor', type: 'http in', z: Z, name: '', url: '/exportar/jornada.csv', method: 'get',
    upload: false, swaggerDoc: '', x: 170, y: 1300, wires: [['pg_csv_jor']] },
  pg('pg_csv_jor', 'vw_jornada_diaria (tudo)', SQL.csvJornada, 400, 1300, [['f_csv']]),
  fn('f_csv', 'monta CSV', montaCsv, 620, 1270, [['http_resp']]),
  { id: 'http_resp', type: 'http response', z: Z, name: '', statusCode: '', headers: {}, x: 820, y: 1270, wires: [] },

  // ---------- Dashboard 2.0 (configuração) ----------
  { id: 'ui_base', type: 'ui-base', name: 'Crachá Inteligente de Ponto', path: '/dashboard',
    appIcon: '', includeClientData: true, acceptsClientConfig: ['ui-notification', 'ui-control'],
    showPathInSidebar: false, headerContent: 'page', navigationStyle: 'default',
    titleBarStyle: 'default', showReconnectNotification: true, notificationDisplayTime: 1,
    showDisconnectNotification: true, allowInstall: false },
  { id: 'ui_theme', type: 'ui-theme', name: 'Tema Ponto',
    colors: { surface: '#ffffff', primary: '#0d74ce', bgPage: '#f4f6fb', groupBg: '#ffffff', groupOutline: '#d9d9e0' },
    sizes: { density: 'default', pagePadding: '12px', groupGap: '12px', groupBorderRadius: '8px', widgetGap: '12px' } },
  { id: 'ui_page', type: 'ui-page', name: 'Painel de Gerência', ui: 'ui_base', path: '/', icon: 'home',
    layout: 'grid', theme: 'ui_theme', order: 1, className: '', visible: 'true', disabled: 'false',
    breakpoints: [
      { name: 'Default', px: '0', cols: '3' }, { name: 'Tablet', px: '576', cols: '6' },
      { name: 'Small Desktop', px: '768', cols: '9' }, { name: 'Desktop', px: '1024', cols: '12' },
    ] },

  G('g_pres', 'Quem está presente agora', 1, 6),
  G('g_disp', 'Dispositivos', 2, 6),
  G('g_filtro', 'Filtros (valem para jornada, semana e histórico)', 3, 12),
  G('g_hoje', 'Jornada do dia', 4, 12),
  G('g_sem', 'Semana (segunda a domingo) do dia escolhido', 5, 6),
  G('g_graf', 'Horas na semana', 6, 6),
  G('g_hist', 'Histórico (últimos 50 pontos)', 7, 12),
  G('g_cmd', 'Comandos para o crachá', 8, 6),
  G('g_csv', 'Exportar (CSV)', 9, 6),

  // ---------- Dashboard 2.0 (widgets) ----------
  texto('x_presentes', 'g_pres', 1, 1280, 540, 20),
  tabela('t_presenca', 'g_pres', 'Presença', 2, 1280, 580),
  tabela('t_disp', 'g_disp', 'Dispositivos', 1, 1280, 820),
  texto('x_gw', 'g_disp', 2, 1280, 860, 15),
  texto('x_filtro', 'g_filtro', 3, 1280, 900, 15),
  tabela('t_hoje', 'g_hoje', 'Jornada', 1, 1280, 640),
  tabela('t_semana', 'g_sem', 'Semana', 1, 1280, 700),
  {
    id: 'g_semana', type: 'ui-chart', z: Z, group: 'g_graf', name: 'Horas na semana', label: '',
    order: 1, chartType: 'bar', category: 'serie', categoryType: 'property',
    xAxisLabel: '', xAxisProperty: 'nome', xAxisPropertyType: 'property', xAxisType: 'category',
    xAxisFormat: '', xAxisFormatType: 'auto', xmin: '', xmax: '', ymin: '', ymax: '',
    yAxisLabel: 'horas', yAxisProperty: 'horas', yAxisPropertyType: 'property', bins: 10,
    action: 'replace', stackSeries: false, pointShape: 'circle', pointRadius: 4, showLegend: true,
    removeOlderUnit: '3600', removeOlderPoints: '',
    colors: ['#0d74ce', '#9ca3af', '#FF7F0E', '#2CA02C', '#A347E1', '#D62728', '#FF9896', '#9467BD', '#C5B0D5'],
    textColor: ['#666666'], textColorDefault: true, gridColor: ['#e5e5e5'], gridColorDefault: true,
    width: 0, height: 6, className: '', interpolation: 'linear', x: 1280, y: 760, wires: [],
  },
  tabela('t_hist', 'g_hist', 'Histórico', 1, 1280, 880),
  texto('x_cmd', 'g_cmd', 5, 1060, 1120, 15),
  {
    id: 'md_csv', type: 'ui-markdown', z: Z, group: 'g_csv', name: 'links CSV', order: 1, width: 0, height: 0,
    content: 'Espelho de ponto para abrir no Excel (separador ";"):\n\n' +
             '- [Baixar histórico completo (CSV)](/exportar/historico.csv)\n' +
             '- [Baixar jornada diária (CSV)](/exportar/jornada.csv)\n',
    x: 1280, y: 1240, wires: [],
  },
]

// A tabela de histórico usa o id t_hist; a da semana usa t_semana (ajuste de nomes das ligações).
const trocas = { t_semana: 't_semana', t_hoje: 't_hoje' }
for (const n of fluxo) if (Array.isArray(n.wires)) n.wires = n.wires.map((w) => w.map((id) => trocas[id] ?? id))

mkdirSync('./nodered-dados', { recursive: true })
writeFileSync('./nodered-dados/flows.json', JSON.stringify(fluxo, null, 2))
console.log('Fluxo gerado em nodered-dados/flows.json (' + fluxo.length + ' itens).')
