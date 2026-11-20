<p align="center">
  <a href="https://deckconnector.com"><img src="site/public/favicon.svg" width="96" height="96" alt="DeckConnector logo"></a>
</p>

<h1 align="center">DeckConnector</h1>

<p align="center">
  Your Stream Deck on the network. A small ESP32-P4 board drives it over USB,<br>
  and Bitfocus Companion and the Elgato App use it like an Elgato Network Dock.
</p>

<p align="center">
  <a href="https://deckconnector.com"><b>Website</b></a> ·
  <a href="https://deckconnector.com/demo">Live demo</a> ·
  <a href="https://deckconnector.com/flash">Flash in the browser</a> ·
  <a href="https://deckconnector.com/docs">Docs</a>
</p>

<p align="center">
  <img src=".github/assets/web-ui.gif" width="860" alt="The web UI with the live deck: a camera key, a dial, the lights folder, the volume, the brightness, a restart and the speaker setting">
</p>

<table align="center">
  <tr>
    <th colspan="2">Home Assistant</th>
  </tr>
  <tr>
    <td valign="top"><img src=".github/assets/ha-connector.gif" width="384" alt="The connector in Home Assistant: brightness, find, screen, speaker, controller, restart, screen off after and screen saver brightness. The brightness and the speaker volume change with the web UI above"></td>
    <td valign="top"><img src=".github/assets/ha-screen.webp" width="330" alt="The deck picture in Home Assistant"><br><img src=".github/assets/ha-deck.webp" width="330" alt="A deck in Home Assistant: screen picture, source and the controls event"></td>
  </tr>
  <tr>
    <td colspan="2" align="center"><a href="https://my.home-assistant.io/redirect/hacs_repository/?owner=FusselTV&repository=DeckConnector&category=integration"><img src="https://my.home-assistant.io/badges/hacs_repository.svg" alt="Open DeckConnector in HACS"></a></td>
  </tr>
</table>

- Companion connects over Satellite or as a Network Dock. The Elgato App sees a Network Dock.
- Several programs connect at once. You pick the one that draws.
- Several decks on one connector.
- Ethernet with PoE, and Wi-Fi with a backup network.
- Home Assistant gets every key press and the deck picture. It sets the brightness and the volume, and picks the program that draws.
- Matter, tested with Home Assistant and Apple Home.
- The web UI shows the live deck, and you can press its keys.
- Log in with a passkey, or confirm the login on the deck.

## Get started

1. Get a [Waveshare ESP32-P4-WIFI6-POE-ETH](https://deckconnector.com/docs/hardware) board.
2. [Flash it in the browser](https://deckconnector.com/flash). There is nothing to install.
3. Plug in the deck and open the address it shows. The setup wizard does the rest.

Everything else is in the [docs](https://deckconnector.com/docs). To build it yourself or to help, see [CONTRIBUTING](.github/CONTRIBUTING.md).

## License

[AGPL-3.0](LICENSE). Warranty, trademarks and an added permission are in [NOTICE.md](NOTICE.md).

Support DeckConnector on [GitHub Sponsors](https://github.com/sponsors/FusselTV) or [Ko-fi](https://ko-fi.com/fusseltv).
