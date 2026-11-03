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
