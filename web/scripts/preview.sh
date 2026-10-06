#!/usr/bin/env bash
# Start/stop the production preview and temporary Cloudflare tunnel.
set -euo pipefail
cd "$(dirname "$0")/.."
preview_dir="$PWD/.preview"
mkdir -p "$preview_dir"
preview_port="${LAB_PORT:-48173}"
detach=(nohup)
if command -v setsid >/dev/null; then detach+=(setsid); fi

alive() { [[ -f "$preview_dir/$1.pid" ]] && kill -0 "$(cat "$preview_dir/$1.pid")" 2>/dev/null; }
stop_one() {
    local name="$1"
    if alive "$name"; then
        local pid
        pid="$(cat "$preview_dir/$name.pid")"
        # Only stop a process created for this preview, never an unrelated reused PID.
        local command
        command="$(ps -p "$pid" -o args= || true)"
        if [[ "$name" == server && "$command" == *scripts/serve.py* ]] ||
           [[ "$name" == tunnel && "$command" == *'cloudflared tunnel --url http://127.0.0.1:'* ]]; then
            kill "$pid"
        fi
    fi
    rm -f "$preview_dir/$name.pid"
}
case "${1:-status}" in
    start)
        [[ -f dist/index.html ]] || { echo 'Run npm run build first.' >&2; exit 1; }
        if ! alive server; then
            "${detach[@]}" python3 -u scripts/serve.py --port "$preview_port" > "$preview_dir/server.log" 2>&1 < /dev/null &
            echo "$!" > "$preview_dir/server.pid"
        fi
        for attempt in {1..30}; do
            if ! alive server; then cat "$preview_dir/server.log" >&2; exit 1; fi
            if curl --fail --silent "http://127.0.0.1:$preview_port/" > /dev/null; then break; fi
            sleep 0.2
        done
        curl --fail --silent "http://127.0.0.1:$preview_port/" > /dev/null
        if ! alive tunnel; then
            command -v cloudflared >/dev/null || { echo 'Install cloudflared to start the external tunnel.' >&2; exit 1; }
            "${detach[@]}" cloudflared tunnel --url "http://127.0.0.1:$preview_port" > "$preview_dir/tunnel.log" 2>&1 < /dev/null &
            echo "$!" > "$preview_dir/tunnel.pid"
        fi
        echo "Local preview: http://127.0.0.1:$preview_port/"
        echo "Tunnel URL: see $preview_dir/tunnel.log or run npm run preview:status."
        ;;
    stop)
        stop_one tunnel
        stop_one server
        echo 'Preview and tunnel stopped.'
        ;;
    status)
        for name in server tunnel; do
            if alive "$name"; then echo "$name running (PID $(cat "$preview_dir/$name.pid"))"; else echo "$name stopped"; fi
        done
        if [[ -f "$preview_dir/tunnel.log" ]]; then
            rg -o 'https://[a-z0-9-]+\.trycloudflare\.com' "$preview_dir/tunnel.log" | tail -1 || true
        fi
        ;;
    *) echo 'Usage: bash scripts/preview.sh start|stop|status' >&2; exit 2 ;;
esac
