declare module 'cloudflare:email' {
  export class EmailMessage {
    readonly from: string
    readonly to: string
    constructor(from: string, to: string, raw: string)
  }
}
