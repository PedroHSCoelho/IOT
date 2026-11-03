// =====================================================================
// broker.mjs — o "CORREIO" do sistema (broker MQTT)
// =====================================================================
// O broker recebe mensagens e as entrega a quem se INSCREVEU no tópico.
// Quem publica não sabe quem vai receber; quem recebe não sabe quem publicou.
//
//   crachá/gateway/PC  --publica-->  [ESTE BROKER]  --entrega-->  Node-RED
//
// Usamos o "aedes" (um broker feito em Node.js) no lugar do Mosquitto, porque
// o Mosquitto para Windows só tem instalador e este notebook não permite
// instalar programas. Para o projeto é idêntico: o protocolo MQTT é o mesmo,
// então depois dá para trocar pelo Mosquitto sem mudar nada no crachá nem no fluxo.
//
// Rodar:  npm run broker        Parar: Ctrl+C
// =====================================================================
import net from 'node:net'
import { Aedes } from 'aedes'

const PORTA = 1883 // porta padrão do MQTT

const aedes = await Aedes.createBroker()
const servidor = net.createServer(aedes.handle)

servidor.listen(PORTA, () => {
  console.log(`[broker] MQTT no ar na porta ${PORTA}. Ctrl+C para parar.`)
})

// Mensagens de acompanhamento, para você VER o que está acontecendo.
aedes.on('client', (c) => console.log(`[broker] conectou: ${c.id}`))
aedes.on('clientDisconnect', (c) => console.log(`[broker] desconectou: ${c.id}`))
aedes.on('subscribe', (subs, c) =>
  console.log(`[broker] ${c?.id} se inscreveu em: ${subs.map((s) => s.topic).join(', ')}`))
aedes.on('publish', (pacote, c) => {
  if (c && !pacote.topic.startsWith('$SYS')) { // ignora mensagens internas do broker
    console.log(`[broker] ${c.id} publicou em "${pacote.topic}": ${pacote.payload.toString().slice(0, 200)}`)
  }
})
