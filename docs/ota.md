# Updates

The Deck-Connector takes signed `.dbfw` files for its own firmware (P4) and for the WLAN chip (C6). A file comes from
a server (`{"t": "ota", "url": ...}`) or from the web UI.

## File format

A 144 byte header, then the image.

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `DBFW` |
| 4 | 1 | format, `1` |
| 5 | 1 | target, `1` P4, `2` C6 |
| 6 | 2 | security level |
| 8 | 4 | image length |
| 12 | 32 | version text |
| 44 | 32 | SHA-256 of the image |
| 76 | 64 | ECDSA P-256 signature (r, s) over bytes 0 to 75 |
| 140 | 4 | reserved |

## What the Deck-Connector checks

1. Header and signature, before it writes anything.
   A file whose security level is below the Deck-Connector's own is refused, so a signed old firmware with a known
   hole cannot go back on. `SECURITY_LEVEL` in `db_ota.c` sets the level, and `dbfw pack` reads it from there.
   Raise it with a security fix. USB flashing still works for any version.
2. The SHA-256 of the whole image, before it switches over.
3. After the restart, the new P4 firmware has two minutes to reach the network and stay up for 20 seconds.
   Otherwise, or after a crash, the bootloader starts the old firmware again.

The C6 gets its image over the P4. A C6 update restarts both chips.

## Slots

The flash holds four firmware slots. An update goes into the slot after the running one, so the slot before it
keeps the last firmware. Slots 2 and 3 are the bench slots for `dbtools.fw`. A slot started by choice has to
confirm itself the same way as a new update. A slot whose image is older than the security level does not start
by choice, because its image does not carry the level. The Deck-Connector notes the level when a file is written
and when a firmware runs.

## On the bench

```
cd tools
uv run python -m dbtools.fw deploy          # sign firmware/build/deckbridge.bin, upload into a bench slot, start it
uv run python -m dbtools.fw use 2           # start slot 2
uv run python -m dbtools.fw ab REV_A REV_B  # build both (cached), start A in one bench slot, B waits in the other
```

`tools/flash.sh` writes over USB and is for recovery and for a new partition table (`flash.sh all`).

## Signing

```
cd tools
uv run python -m dbtools.dbfw keygen        # once; key in ~/.config/deckbridge/ota_key.pem
uv run python -m dbtools.dbfw pack --target p4 --version "$(git describe)" ../firmware/build/deckbridge.bin out.dbfw
```

`keygen` writes the public key to `firmware/components/db_ota/ota_pubkey.c`. A Deck-Connector only accepts files signed
with the key it was built with. Losing the private key means the next firmware has to go on over USB.
