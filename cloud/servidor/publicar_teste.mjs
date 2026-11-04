// =====================================================================
// publicar_teste.mjs — finge ser um CRACHÁ, para testar sem o hardware
// =====================================================================
// Publica uma mensagem MQTT igual à que o gateway/PC mandariam.
//
// Uso (com o broker e o Node-RED rodando):
//   npm run publicar-teste                      -> botão do crachá 101
//   npm run publicar-teste -- ble PRESENTE      -> BLE "chegou" do crachá 101
//   npm run publicar-teste -- botao 101         -> botão do crachá 101
//   npm run publicar-teste -- botao 999         -> crachá desconhecido (vai para rejeitados)
// =====================================================================
import mqtt from 'mqtt'

// Modo "gateway": finge ser o gateway avisando o próprio estado (mensagem RETAINED).
//   npm run publicar-teste -- gateway online [rssi] [snr]   -> online + qualidade do LoRa
//   npm run publicar-teste -- gateway offline               -> offline avisado
//   npm run publicar-teste -- gateway cair                  -> online, depois some SEM avisar:
//                                                              o broker publica o Last Will (offline)
const TOPICO_GW = 'unifesp/ponto/gateway/status'
if (process.argv[2] === 'gateway') {
  const [, , , modo = 'online', rssi = '-71', snr = '9.5'] = process.argv
  const will = { topic: TOPICO_GW, payload: JSON.stringify({ estado: 'offline' }), qos: 1, retain: true }
  const c = mqtt.connect('mqtt://localhost:1883', { clientId: 'gateway-simulado', will: modo === 'cair' ? will : undefined })
  c.on('connect', () => {
    const corpo = modo === 'offline'
      ? { estado: 'offline' }
      : { estado: 'online', rssi: Number(rssi), snr: Number(snr) }
    c.publish(TOPICO_GW, JSON.stringify(corpo), { qos: 1, retain: true }, () => {
      console.log('Publicado (retained) em ' + TOPICO_GW + ':', JSON.stringify(corpo))
      if (modo === 'cair') {
        console.log('Derrubando a conexão sem avisar (o broker deve publicar o Last Will)...')
        c.stream.destroy()           // corta o cabo: sem DISCONNECT educado
        setTimeout(() => process.exit(0), 500)
      } else {
        c.end()
      }
    })
  })
  c.on('error', (e) => { console.error('Erro MQTT (o broker está rodando?):', e.message); process.exit(1) })
} else if (process.argv[2] === 'ack') {
// Modo "ack": finge ser o crachá RESPONDENDO a um comando do painel.
//   npm run publicar-teste -- ack <id_do_comando> [ok|falha] [versao]
//   ex.: npm run publicar-teste -- ack 12 ok 2.0
  const [, , , idTxt, resultado = 'ok', versao] = process.argv
  const ack = { id: Number(idTxt), ok: resultado !== 'falha', ...(versao ? { versao } : {}) }
  const c = mqtt.connect('mqtt://localhost:1883', { clientId: 'cracha-simulado-ack' })
  c.on('connect', () => {
    c.publish('unifesp/ponto/cmd/101/ack', JSON.stringify(ack), { qos: 1 }, () => {
      console.log('Publicado em unifesp/ponto/cmd/101/ack:', JSON.stringify(ack))
      c.end()
    })
  })
  c.on('error', (e) => { console.error('Erro MQTT (o broker está rodando?):', e.message); process.exit(1) })
} else {
  principal()
}

function principal() {
const [origem = 'botao', extra] = process.argv.slice(2)
const badge = origem === 'botao' && extra ? Number(extra) : 101

// "seq" precisa ser único por crachá+origem. Usamos a hora para nunca repetir
// (o crachá real usa um contador que só cresce).
const seq = Math.floor(Date.now() / 1000)

const mensagem = {
  ts: new Date().toISOString(),
  badge,
  origem,
  seq,
  ...(origem === 'ble'
    ? { estado: extra ?? 'PRESENTE', rssi_ble: -58 }
    : { fila: false, rssi_lora: -71, snr: 9.5 }),
}

const cliente = mqtt.connect('mqtt://localhost:1883', { clientId: 'cracha-simulado' })
cliente.on('connect', () => {
  cliente.publish('unifesp/ponto/evento', JSON.stringify(mensagem), { qos: 1 }, () => {
    console.log('Publicado em unifesp/ponto/evento:', JSON.stringify(mensagem))
    cliente.end()
  })
})
cliente.on('error', (e) => { console.error('Erro MQTT (o broker está rodando?):', e.message); process.exit(1) })
}
