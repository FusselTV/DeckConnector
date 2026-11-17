"""The update check reads the repository's release feed and offers only a newer release; OTA by URL follows
redirects and speaks HTTPS, as a GitHub release file needs. A feed and files served from this machine stand in for
GitHub, so nothing is installed: the files are no .dbfw."""

import http.server
import json
import threading
import time
import urllib.parse

import pytest
from conftest import api

from dbtools.server import local_ip

PORT = 8771


def repo() -> str:
    return f"http://{local_ip()}:{PORT}/owner/repo"


def atom(*tags: str) -> str:
    """GitHub's release feed: newest first, each entry links its release page."""
    entries = "".join(
        f"<entry><id>tag:github.com,2008:Repository/1/{t}</id>"
        f'<link rel="alternate" type="text/html" href="{repo()}/releases/tag/{t}"/><title>{t}</title></entry>'
        for t in tags
    )
    return f'<?xml version="1.0" encoding="UTF-8"?><feed xmlns="http://www.w3.org/2005/Atom">{entries}</feed>'


FEEDS: dict[str, str] = {}


class Feed(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        path = urllib.parse.urlparse(self.path).path
        if path.startswith("/feed/") and path[6:] in FEEDS:
            body = FEEDS[path[6:]].encode()
            self.send_response(200)
        elif path.startswith("/owner/repo/releases/download/"):  # what GitHub does: a redirect to its storage
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
    yield lambda name: f"http://{local_ip()}:{PORT}/feed/{name}"
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
    FEEDS["new"] = atom("v99.0.0-rc1", "v98.0.0", "v1.0.0", "junk-tag")
    current = check()["current"]
    u = check(feed("new"))
    latest = "v99.0.0-rc1" if "-rc" in current else "v98.0.0"
    assert u["latest"] == latest, u
    assert u["url"] == f"{repo()}/releases/download/{latest}/deckconnector-{latest}.dbfw"
    assert u["notes"] == f"{repo()}/releases/tag/{latest}"


def test_an_older_release_is_named_but_not_offered(feed):
    FEEDS["old"] = atom("v0.0.1")
    u = check(feed("old"))
    assert u["latest"] == "v0.0.1" and "url" not in u


def test_a_feed_without_releases_and_a_missing_feed(feed):
    FEEDS["empty"] = atom()
    assert check(feed("empty"))["code"] == "no_releases"
    assert check(feed("nothing-here"))["code"] == "no_releases"  # the feed of a repository without releases: 404


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
def test_ota_by_url_follows_redirects_over_https(feed, url, outcome):
    since = api("GET", "/api/log")[1].decode().splitlines()[-1]
    code, reply = api("POST", "/api/ota/url", json.dumps({"url": url}).encode())
    assert code == 200, reply
    line = wait_for_log(outcome, since)
    assert line, f"no '{outcome}': the download did not get through the redirect"
