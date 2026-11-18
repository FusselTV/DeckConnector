declare module 'cloudflare:sockets' {
  type Socket = {
    readable: ReadableStream<Uint8Array>
    writable: WritableStream<Uint8Array>
    startTls(): Socket
    close(): Promise<void>
  }
  export function connect(
    address: { hostname: string; port: number },
    options?: { secureTransport?: 'off' | 'on' | 'starttls' },
  ): Socket
}
