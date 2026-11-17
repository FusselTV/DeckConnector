import { connect } from 'cloudflare:sockets'

/** One mail over SMTP submission (port 587, STARTTLS, AUTH PLAIN). Throws with the server's reply on any refusal. */
export async function send(o: { host: string; user: string; pass: string; from: string; to: string; mail: string }) {
  let sock = connect({ hostname: o.host, port: 587 }, { secureTransport: 'starttls' })
  // A stalled server would hold the visitor's request open: closing ends the pending read, which then throws.
  const stall = setTimeout(() => void sock.close().catch(() => {}), 15_000)
  try {
    let io = talk(sock)
    await io.expect(220)
    await io.cmd('EHLO deckconnector.com', 250)
    await io.cmd('STARTTLS', 220)
    io.release()
    sock = sock.startTls()
    io = talk(sock)
    await io.cmd('EHLO deckconnector.com', 250)
    await io.cmd(`AUTH PLAIN ${btoa(`\0${o.user}\0${o.pass}`)}`, 235)
    await io.cmd(`MAIL FROM:<${o.from}>`, 250)
    await io.cmd(`RCPT TO:<${o.to}>`, 250)
    await io.cmd('DATA', 354)
    // A line that starts with a dot gets a second one (RFC 5321 4.5.2).
    await io.cmd(`${o.mail.replace(/^\./gm, '..')}\r\n.`, 250)
    await io.cmd('QUIT', 221)
  } finally {
    clearTimeout(stall)
    await sock.close().catch(() => {})
  }
}

type Socket = ReturnType<typeof connect>

function talk(sock: Socket) {
  const reader = sock.readable.getReader()
  const writer = sock.writable.getWriter()
  const dec = new TextDecoder()
  let buf = ''
  // A reply is done at the line whose code is followed by a space, not a dash.
  const reply = async () => {
    for (;;) {
      const done = /(?:^|\r\n)(\d{3})(?: [^\r\n]*)?\r\n/.exec(buf)
      if (done) {
        const text = buf.slice(0, done.index + done[0].length)
        buf = buf.slice(text.length)
        return { code: Number(done[1]), text }
      }
      const { value, done: eof } = await reader.read()
      if (eof) throw new Error(`smtp: connection closed after ${JSON.stringify(buf)}`)
      buf += dec.decode(value, { stream: true })
    }
  }
  const expect = async (code: number) => {
    const r = await reply()
    if (r.code !== code) throw new Error(`smtp: wanted ${code}, got ${r.text.trim()}`)
  }
  return {
    expect,
    async cmd(line: string, code: number) {
      await writer.write(new TextEncoder().encode(`${line}\r\n`))
      await expect(code)
    },
    release() {
      reader.releaseLock()
      writer.releaseLock()
    },
  }
}
