"""The update check reads the repository's release feed and offers only a newer release; OTA by URL follows
redirects and speaks HTTPS, as a GitHub release file needs. Feeds come from public GitHub repositories; files served
from this machine stand in for release files, so nothing is installed: they are no .dbfw."""

import http.server
import json
import threading
import time
import urllib.parse

import pytest
from conftest import api

from dbtools.server import local_ip

PORT = 8771


class Feed(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        path = urllib.parse.urlparse(self.path).path
        if path.startswith("/owner/repo/releases/download/"):  # what GitHub does: a redirect to its storage
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
def server():
    srv = http.server.ThreadingHTTPServer(("0.0.0.0", PORT), Feed)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    yield
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


@pytest.mark.sim
@pytest.mark.parametrize(
    "feed", ["http://github.com/a/b/releases.atom", "https://github.com.example/a.atom", "file:///x"]
)
def test_a_feed_off_github_is_refused(feed):
    code, body = api("GET", "/api/update/check?feed=" + urllib.parse.quote(feed, safe=""))
    assert code == 400 and b"feed" in body, body


def test_a_newer_release_is_offered():
    u = check("https://github.com/espressif/esptool/releases.atom")
    if u.get("code") == "rate_limited":
        pytest.skip("GitHub limits checks")
    assert "url" in u, u
    latest = u["latest"]
    assert u["url"] == f"https://github.com/espressif/esptool/releases/download/{latest}/deckconnector-{latest}.dbfw"
    assert u["notes"] == f"https://github.com/espressif/esptool/releases/tag/{latest}"


def test_a_repository_without_releases():
    assert check("https://github.com/FusselTV/no-such-repository/releases.atom")["code"] in (
        "no_releases",
        "rate_limited",
    )


def wait_for_log(text: str, since: str, secs: float = 30) -> str | None:
    """`text` in a line after `since`; the log is a ring, so a line count would stop moving once it is full."""
    end = time.time() + secs
    while time.time() < end:
        log = api("GET", "/api/log")[1].decode()
        lines = log[log.rfind(since) + len(since) :].splitlines() if since in log else []
        hit = next((line for line in lines if text in line), None)
        if hit:
            return hit
        time.sleep(0.5)
    return None


@pytest.mark.parametrize(
    "url, outcome",
    [
        (f"http://{local_ip()}:{PORT}/owner/repo/releases/download/v99.0.0/x.dbfw", "not a .dbfw file"),  # 302 here
        # a public GitHub release file, by HTTPS and a redirect to its storage; far too big for a slot
        (
            "https://github.com/espressif/esptool/releases/download/v5.6.0/esptool-v5.6.0-linux-amd64.tar.gz",
            "larger than a firmware slot",
        ),
    ],
)
def test_ota_by_url_follows_redirects_over_https(server, url, outcome):
    since = api("GET", "/api/log")[1].decode().splitlines()[-1]
    code, reply = api("POST", "/api/ota/url", json.dumps({"url": url}).encode())
    assert code == 200, reply
    line = wait_for_log(outcome, since)
    assert line, f"no '{outcome}': the download did not get through the redirect"
