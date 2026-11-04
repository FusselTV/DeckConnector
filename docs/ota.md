# Updates

The bridge takes signed `.dbfw` files for its own firmware (P4) and for the WLAN chip (C6). A file comes from
a server (`{"t": "ota", "url": ...}`) or from the web UI.

## File format

A 144 byte header, then the image.

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `DBFW` |
| 4 | 1 | format, `1` |
| 5 | 1 | target, `1` P4, `2` C6 |
| 6 | 2 | reserved |
| 8 | 4 | image length |
| 12 | 32 | version text |
| 44 | 32 | SHA-256 of the image |
| 76 | 64 | ECDSA P-256 signature (r, s) over bytes 0 to 75 |
| 140 | 4 | reserved |

## What the bridge checks

1. Header and signature, before it writes anything.
2. The SHA-256 of the whole image, before it switches over.
3. After the restart, the new P4 firmware has two minutes to reach the network and stay up for 20 seconds.
   Otherwise, or after a crash, the bootloader starts the old firmware again.

The C6 gets its image over the P4. A C6 update restarts both chips.

## Signing

```
cd tools
uv run python -m dbtools.dbfw keygen        # once; key in ~/.config/deckbridge/ota_key.pem
uv run python -m dbtools.dbfw pack --target p4 --version "$(git describe)" ../firmware/build/deckbridge.bin out.dbfw
```

`keygen` writes the public key to `firmware/components/db_ota/ota_pubkey.c`. A bridge only accepts files signed
with the key it was built with. Losing the private key means the next firmware has to go on over USB.
