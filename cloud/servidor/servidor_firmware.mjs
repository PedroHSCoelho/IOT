// =====================================================================
// servidor_firmware.mjs — entrega o arquivo .bin do OTA ao crachá pela rede
// =====================================================================
// O que é OTA? "Over The Air": atualizar o programa do crachá sem cabo USB. O crachá liga o
// Wi-Fi, BAIXA o arquivo .bin de um endereço http:// e se reinicia com a versão nova.
// Este programa é o "site" de onde ele baixa.
//
// Por que um servidor separado e não o do Node-RED? O Node-RED escuta só neste computador
// (127.0.0.1) porque o painel não tem senha. O crachá, porém, precisa alcançar o notebook
// pela rede (hotspot). Então este servidor fica numa porta própria e entrega SÓ a pasta
// "firmware/": nada do painel, do banco ou do resto do projeto fica exposto.
//
// Uso:
//   1) copie o .bin para cloud/servidor/firmware/   (ex.: cracha_v2.bin)
//   2) npm run firmware-http
//   3) no painel, URL do .bin = o endereço que este programa imprime (use o IP do hotspot)
//
// Segurança: só GET/HEAD, só arquivos .bin com nome simples (letras, números, _ - .), sem
// listar a pasta, sem subpastas. Porta: 8080 (mude com FIRMWARE_PORT=...).
// Obs.: HTTP puro (sem TLS) é aceitável aqui por ser rede local de demonstração; o .bin
// não é segredo. Ligue-o só durante a demonstração.
// =====================================================================
import { createReadStream } from 'node:fs'
import { stat } from 'node:fs/promises'
import { createServer } from 'node:http'
import { networkInterfaces } from 'node:os'
import { join, resolve } from 'node:path'

const PASTA = resolve('./firmware')
const PORTA = Number(process.env.FIRMWARE_PORT || 8080)
const NOME_VALIDO = /^[A-Za-z0-9_.-]+\.bin$/

const servidor = createServer(async (req, res) => {
  const responde = (codigo, texto) => { res.writeHead(codigo, { 'Content-Type': 'text/plain; charset=utf-8' }); res.end(texto) }

  if (req.method !== 'GET' && req.method !== 'HEAD') return responde(405, 'Metodo nao permitido')

  // Só aceita /firmware/<nome>.bin  (ou /<nome>.bin). Nada de ".." nem subpastas.
  let nome
  try { nome = decodeURIComponent(new URL(req.url, 'http://x').pathname.split('/').pop() || '') }
  catch { return responde(400, 'Endereco invalido') }
  if (!NOME_VALIDO.test(nome) || nome.includes('..')) return responde(404, 'Nao encontrado')

  const caminho = join(PASTA, nome)
  let info
  try { info = await stat(caminho) } catch { return responde(404, 'Nao encontrado') }
  if (!info.isFile()) return responde(404, 'Nao encontrado')

  console.log(new Date().toLocaleTimeString('pt-BR') + '  ' + req.method + ' ' + nome + '  (' + info.size + ' bytes)  de ' + req.socket.remoteAddress)
  res.writeHead(200, { 'Content-Type': 'application/octet-stream', 'Content-Length': info.size })
  if (req.method === 'HEAD') return res.end()
  createReadStream(caminho).pipe(res)
})

servidor.listen(PORTA, '0.0.0.0', () => {
  console.log('Servidor de firmware ligado. Pasta: ' + PASTA)
  console.log('Endereços para colar no painel (troque NOME.bin pelo seu arquivo):')
  for (const lista of Object.values(networkInterfaces()))
    for (const i of lista || [])
      if (i.family === 'IPv4' && !i.internal) console.log('  http://' + i.address + ':' + PORTA + '/firmware/NOME.bin')
  console.log('Ctrl+C para desligar.')
})
servidor.on('error', (e) => { console.error('Erro ao iniciar:', e.message); process.exit(1) })
