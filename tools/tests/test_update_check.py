"""The update check asks GitHub for releases and offers only a newer one; OTA by URL follows redirects and speaks
HTTPS, as a GitHub release file needs. A release list and files served from this machine stand in for GitHub, so
nothing is installed: the files are no .dbfw."""

import http.server
import json
import threading
import time
import urllib.parse

import pytest
from conftest import api

from dbtools.server import local_ip

PORT = 8771


def release(tag: str, pre: bool = False, draft: bool = False) -> dict:
    return {
        "tag_name": tag,
        "prerelease": pre,
        "draft": draft,
        "html_url": f"https://example.org/releases/tag/{tag}",
        "assets": [
            {"name": f"deckconnector-{tag}.dbfw", "browser_download_url": f"http://{local_ip()}:{PORT}/get/{tag}"},
            {"name": "deckconnector.elf", "browser_download_url": "http://example.org/elf"},
        ],
    }


LISTS: dict[str, list] = {}


class Feed(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        path = urllib.parse.urlparse(self.path).path
        if path.startswith("/list/"):
            body = json.dumps(LISTS[path[6:]]).encode()
            self.send_response(200)
        elif path.startswith("/get/"):  # what GitHub does: a redirect to its storage
            self.send_response(302)
            self.send_header("Location", f"http://{local_ip()}:{PORT}/file")
            body = b""
        elif path == "/file":
            body = b"NOT A DBFW FILE" * 100
            self.send_response(200)
        else:
            body = b""
            self.send_response(404)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *a):
        pass


@pytest.fixture(scope="module")
def feed():
    srv = http.server.ThreadingHTTPServer(("0.0.0.0", PORT), Feed)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    yield lambda name: f"http://{local_ip()}:{PORT}/list/{name}"
    srv.shutdown()
    srv.server_close()


def check(feed_url: str | None = None) -> dict:
    q = "?feed=" + urllib.parse.quote(feed_url, safe="") if feed_url else ""
    code, body = api("GET", "/api/update/check" + q)
    assert code == 200
    return json.loads(body)


@pytest.mark.sim
def test_the_check_names_the_running_firmware():
    u = check()
    assert u["current"].startswith("v") and "url" not in u, u


def test_github_is_asked_over_https():
    """A private repository answers 404; a public one lists releases. Either way the TLS connection worked."""
    u = check()
    assert u.get("latest") or u.get("code") in ("no_releases", "rate_limited"), u


def test_a_newer_release_is_offered_and_a_prerelease_only_to_a_prerelease(feed):
    LISTS["new"] = [release("v99.0.0-rc1", pre=True), release("v98.0.0"), release("v100.0.0", draft=True)]
    current = check()["current"]
    u = check(feed("new"))
    pre = "-rc" in current
    assert u["latest"] == ("v99.0.0-rc1" if pre else "v98.0.0"), u
    assert u["url"].endswith("/get/" + u["latest"])
    assert u["notes"].endswith("/tag/" + u["latest"])


def test_an_older_release_is_named_but_not_offered(feed):
    LISTS["old"] = [release("v0.0.1")]
    u = check(feed("old"))
    assert u["latest"] == "v0.0.1" and "url" not in u


def wait_for_log(text: str, since: int, secs: float = 30) -> str | None:
    end = time.time() + secs
    while time.time() < end:
        lines = api("GET", "/api/log")[1].decode().splitlines()[since:]
        hit = next((line for line in lines if text in line), None)
        if hit:
            return hit
        time.sleep(0.5)
    return None


@pytest.mark.parametrize(
    "url, outcome",
    [
        (f"http://{local_ip()}:{PORT}/get/v99.0.0", "not a .dbfw file"),  # 302 to this machine
        # a public GitHub release file, by HTTPS and a redirect to its storage; far too big for a slot
        (
            "https://github.com/espressif/esptool/releases/download/v5.6.0/esptool-v5.6.0-linux-amd64.tar.gz",
            "larger than a firmware slot",
        ),
    ],
)
def test_ota_by_url_follows_redirects_over_https(feed, url, outcome):
    since = len(api("GET", "/api/log")[1].decode().splitlines())
    code, reply = api("POST", "/api/ota/url", json.dumps({"url": url}).encode())
    assert code == 200, reply
    line = wait_for_log(outcome, since)
    assert line, f"no '{outcome}': the download did not get through the redirect"
