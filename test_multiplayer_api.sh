#!/usr/bin/env bash
# Curl-compatible raw TCP smoke test for the multiplayer API in api.md.
#
# The application host must already be running and listening. The script does
# not start a GUI, create a host, or transfer a pack. It connects one or more
# virtual clients through curl's telnet transport and sends line-protocol
# commands.
#
# Examples:
#   SIO_PACK=/path/to/pack ./test_multiplayer_api.sh
#   SIO_HOST=10.147.20.3 SIO_PACK=/path/to/pack \
#       SIO_CLIENTS=2 SIO_REQUIRE_GAME_START=1 ./test_multiplayer_api.sh
#   SIO_PACK_HASH=<64-hex-sha256> ./test_multiplayer_api.sh --no-actions
#
# Useful environment variables:
#   SIO_HOST                 Host address (default: 127.0.0.1)
#   SIO_PORT                 Host port (default: 32323)
#   SIO_PACK                 Local pack directory used to calculate its hash
#   SIO_PACK_HASH            Use an already calculated hash instead of SIO_PACK
#   SIO_CLIENTS              Number of virtual remote clients, 1..4 (default: 1)
#   SIO_WAIT                 Seconds to wait for each protocol event (default: 8)
#   SIO_CONNECT_TIMEOUT      Curl connection timeout (default: 3)
#   SIO_REQUIRE_GAME_START   Fail if the host does not start (default: 0)
#   SIO_TEST_PAUSE           Exercise pause/resume during picking (default: 0)
#   SIO_TEST_RECONNECT       Reconnect client 1 and require a snapshot (default: 0)
#   SIO_TEST_ERRORS          Send an unknown command and require ERROR (default: 1)
#   SIO_NO_ACTIONS           Stop after handshake/lobby (default: 0)
#   SIO_THEME                Question theme for the smoke selection (default: 0)
#   SIO_QUESTION             Question index for the smoke selection (default: 0)
#   SIO_SUBMIT_ANSWER        Submit SIO_ANSWER after winning reaction (default: 0)
#   SIO_ANSWER_TYPE          Text, Select, or Point (default: Text)
#   SIO_ANSWER               Text answer (default: empty)
#   SIO_SELECT_ID            Select option ID (default: A)
#   SIO_POINT_X              Point x coordinate (default: 0.5)
#   SIO_POINT_Y              Point y coordinate (default: 0.5)
#
# Requirements: bash 5+, curl with telnet support, timeout, find, stat, and
# sha256sum. The script intentionally uses no nc, socat, Python, or jq.

set -Eeuo pipefail

readonly SCRIPT_NAME="$(basename "$0")"
readonly DEFAULT_HOST="127.0.0.1"
readonly DEFAULT_PORT="32323"
readonly DEFAULT_WAIT="8"
readonly DEFAULT_CONNECT_TIMEOUT="3"
readonly DEFAULT_CLIENTS="1"

HOST="${SIO_HOST:-$DEFAULT_HOST}"
PORT="${SIO_PORT:-$DEFAULT_PORT}"
PACK_DIR="${SIO_PACK:-}"
PACK_HASH="${SIO_PACK_HASH:-}"
CLIENT_COUNT="${SIO_CLIENTS:-$DEFAULT_CLIENTS}"
WAIT_SECONDS="${SIO_WAIT:-$DEFAULT_WAIT}"
CONNECT_TIMEOUT="${SIO_CONNECT_TIMEOUT:-$DEFAULT_CONNECT_TIMEOUT}"
REQUIRE_GAME_START="${SIO_REQUIRE_GAME_START:-0}"
TEST_PAUSE="${SIO_TEST_PAUSE:-0}"
TEST_RECONNECT="${SIO_TEST_RECONNECT:-0}"
TEST_ERRORS="${SIO_TEST_ERRORS:-1}"
NO_ACTIONS="${SIO_NO_ACTIONS:-0}"
THEME_INDEX="${SIO_THEME:-0}"
QUESTION_INDEX="${SIO_QUESTION:-0}"
SUBMIT_ANSWER="${SIO_SUBMIT_ANSWER:-0}"
ANSWER_TYPE="${SIO_ANSWER_TYPE:-Text}"
ANSWER_TEXT="${SIO_ANSWER:-}"
SELECT_ID="${SIO_SELECT_ID:-A}"
POINT_X="${SIO_POINT_X:-0.5}"
POINT_Y="${SIO_POINT_Y:-0.5}"

TMP_DIR=""
MATCH_LINE=""

# Associative arrays are intentionally kept in the shell so each client has a
# persistent curl process and a persistent authenticated TCP connection.
declare -a CLIENT_NAMES=()
declare -A CLIENT_IN CLIENT_OUT CLIENT_PID CLIENT_TOKEN CLIENT_ID CLIENT_SESSION
# shellcheck disable=SC2034
# CLIENT_ACTION is read and written by next_action.
declare -A CLIENT_ACTION CLIENT_LAST_LINE CLIENT_PENDING

usage() {
      cat <<EOF
Usage: $SCRIPT_NAME [options]

Options:
  --host ADDRESS       Host address (default: $HOST)
  --port PORT          Host port (default: $PORT)
  --pack DIRECTORY     Calculate the pack manifest hash from DIRECTORY
  --hash SHA256        Use SHA256 instead of calculating a pack hash
  --clients COUNT      Virtual remote clients, 1..4 (default: $CLIENT_COUNT)
  --wait SECONDS       Event wait timeout (default: $WAIT_SECONDS)
  --require-game       Fail when the host does not start a game
  --test-pause         Test pause/resume during question picking
  --test-reconnect     Reconnect virtual client p1 and require a snapshot
  --no-errors          Do not run the malformed-command check
  --no-actions         Stop after handshake/lobby
  --help               Show this help

The host must already be running and listening at the selected address/port.
This script is a client smoke test, not a host launcher. A matching pack hash
is required; use --pack or --hash. See api.md for the complete wire protocol.
EOF
}

log() {
      printf '[test] %s\n' "$*"
}

warn() {
      printf '[test] WARNING: %s\n' "$*" >&2
}

fail() {
      printf '[test] ERROR: %s\n' "$*" >&2
      exit 1
}

require_command() {
      command -v "$1" >/dev/null 2>&1 ||
            fail "required command not found: $1"
}

is_nonnegative_integer() {
      [[ "$1" =~ ^[0-9]+$ ]]
}

parse_options() {
      while (($# > 0)); do
            case "$1" in
            --host)
                  (($# >= 2)) || fail "--host requires an address"
                  HOST=$2
                  shift 2
                  ;;
            --port)
                  (($# >= 2)) || fail "--port requires a port"
                  PORT=$2
                  shift 2
                  ;;
            --pack)
                  (($# >= 2)) || fail "--pack requires a directory"
                  PACK_DIR=$2
                  shift 2
                  ;;
            --hash)
                  (($# >= 2)) || fail "--hash requires a SHA-256 value"
                  PACK_HASH=$2
                  shift 2
                  ;;
            --clients)
                  (($# >= 2)) || fail "--clients requires a count"
                  CLIENT_COUNT=$2
                  shift 2
                  ;;
            --wait)
                  (($# >= 2)) || fail "--wait requires seconds"
                  WAIT_SECONDS=$2
                  shift 2
                  ;;
            --require-game)
                  REQUIRE_GAME_START=1
                  shift
                  ;;
            --test-pause)
                  TEST_PAUSE=1
                  shift
                  ;;
            --test-reconnect)
                  TEST_RECONNECT=1
                  shift
                  ;;
            --no-errors)
                  TEST_ERRORS=0
                  shift
                  ;;
            --no-actions)
                  NO_ACTIONS=1
                  shift
                  ;;
            --help|-h)
                  usage
                  exit 0
                  ;;
            *)
                  fail "unknown option: $1 (try --help)"
                  ;;
            esac
      done
}

# Percent-encode a UTF-8 value without depending on Python, jq, or external
# URL-encoding utilities. The default smoke-test values are ASCII, but this
# also makes nickname/answer overrides safe for spaces and separators.
urlencode() {
      local value=$1
      local output=""
      local character
      local encoded
      local index
      local LC_ALL=C

      for ((index = 0; index < ${#value}; ++index)); do
            character=${value:index:1}
            case "$character" in
            [a-zA-Z0-9._~-])
                  output+=$character
                  ;;
            *)
                  printf -v encoded '%%%02X' "'${character}"
                  output+=$encoded
                  ;;
            esac
      done
      printf '%s' "$output"
}

file_size() {
      if stat -c '%s' -- "$1" >/dev/null 2>&1; then
            stat -c '%s' -- "$1"
      else
            stat -f '%z' -- "$1"
      fi
}

calculate_pack_hash() {
      local root=$1
      local file
      local relative
      local size
      local digest

      [[ -d "$root" ]] ||
            fail "pack directory does not exist: $root (use the unpacked game-pack directory)"

      # This is the canonical manifest described in plan.md: sorted relative
      # path, byte size, and per-file SHA-256, followed by a SHA-256 of the
      # resulting UTF-8 lines.
      while IFS= read -r -d '' file; do
            relative=${file#"$root"/}
            size=$(file_size "$file")
            digest=$(sha256sum -- "$file" | awk '{print $1}')
            printf '%s\t%s\t%s\n' "$relative" "$size" "$digest"
      done < <(find "$root" -type f -print0) |
            LC_ALL=C sort |
            sha256sum |
            awk '{print $1}'
}

validate_configuration() {
      is_nonnegative_integer "$PORT" || fail "port must be an integer: $PORT"
      ((PORT >= 1 && PORT <= 65535)) || fail "port out of range: $PORT"
      is_nonnegative_integer "$CLIENT_COUNT" ||
            fail "client count must be an integer: $CLIENT_COUNT"
      ((CLIENT_COUNT >= 1 && CLIENT_COUNT <= 4)) ||
            fail "client count must be between 1 and 4"
      is_nonnegative_integer "$WAIT_SECONDS" ||
            fail "wait must be an integer: $WAIT_SECONDS"
      ((WAIT_SECONDS >= 1)) || fail "wait must be at least 1 second"
      is_nonnegative_integer "$CONNECT_TIMEOUT" ||
            fail "connect timeout must be an integer: $CONNECT_TIMEOUT"
      [[ "$ANSWER_TYPE" == Text || "$ANSWER_TYPE" == Select ||
            "$ANSWER_TYPE" == Point ]] ||
            fail "unsupported SIO_ANSWER_TYPE: $ANSWER_TYPE"

      if [[ -n "$PACK_DIR" ]]; then
            log "calculating pack hash: $PACK_DIR"
            PACK_HASH=$(calculate_pack_hash "$PACK_DIR")
      fi

      [[ "$PACK_HASH" =~ ^[[:xdigit:]]{64}$ ]] ||
            fail "provide --pack or a 64-hex --hash (got: ${PACK_HASH:-empty})"
      PACK_HASH=${PACK_HASH,,}
}

# Drain large profile frames outside Bash. Bash's timed read handles long lines
# one byte at a time, which made multi-megabyte profile transfers appear hung
# and flooded the terminal with base64 data.
filter_protocol_output() {
      LC_ALL=C sed -u -E '/^PROFILE_CHUNK[[:space:]]/d'
}

# Start one persistent curl/telnet client. Named coprocesses are used instead
# of FIFOs so a failed connection cannot block the parent while opening a
# writer. The output descriptor is consumed line-by-line by wait_for().
start_client() {
      local name=$1
      local token=$2
      local coprocess_name="CP_${name^^}"
      local url="telnet://${HOST}:${PORT}"
      local quoted_url
      local quoted_error
      local error_file="$TMP_DIR/${name}.curl.stderr"
      local input_fd
      local output_fd
      local stable_input_fd
      local stable_output_fd
      local process_id

      printf -v quoted_url '%q' "$url"
      printf -v quoted_error '%q' "$error_file"
      eval "coproc $coprocess_name { curl --silent --show-error --no-buffer --connect-timeout $CONNECT_TIMEOUT $quoted_url 2>$quoted_error | filter_protocol_output; }"
      eval "input_fd=\${${coprocess_name}[1]}"
      eval "output_fd=\${${coprocess_name}[0]}"
      eval "process_id=\${${coprocess_name}_PID}"
      # Bash may close coprocess descriptors in command substitutions. Keep
      # stable duplicates for the lifetime of the client.
      eval "exec {stable_input_fd}>&$input_fd"
      eval "exec {stable_output_fd}<&$output_fd"
      eval "exec ${input_fd}>&-"
      eval "exec ${output_fd}<&-"

      CLIENT_IN["$name"]=$stable_input_fd
      CLIENT_OUT["$name"]=$stable_output_fd
      CLIENT_PID["$name"]=$process_id
      CLIENT_TOKEN["$name"]=$token
      CLIENT_ID["$name"]=""
      CLIENT_SESSION["$name"]=""
      CLIENT_ACTION["$name"]=0
      CLIENT_LAST_LINE["$name"]=""
      CLIENT_PENDING["$name"]=""
      local known_name
      for known_name in "${CLIENT_NAMES[@]-}"; do
            if [[ "$known_name" == "$name" ]]; then
                  log "$name reconnected through curl (pid $process_id)"
                  return
            fi
      done
      CLIENT_NAMES+=("$name")
      log "$name connected through curl (pid $process_id)"
}

stop_client() {
      local name=$1
      local input_fd=${CLIENT_IN[$name]-}
      local output_fd=${CLIENT_OUT[$name]-}
      local process_id=${CLIENT_PID[$name]-}

      if [[ -n "$input_fd" ]]; then
            eval "exec ${input_fd}>&-" 2>/dev/null || true
      fi
      if [[ -n "$process_id" ]] && kill -0 "$process_id" 2>/dev/null; then
            kill "$process_id" 2>/dev/null || true
      fi
      if [[ -n "$process_id" ]]; then
            wait "$process_id" 2>/dev/null || true
      fi
      if [[ -n "$output_fd" ]]; then
            eval "exec ${output_fd}<&-" 2>/dev/null || true
      fi
}

cleanup() {
      local name
      for name in "${CLIENT_NAMES[@]-}"; do
            [[ -n "$name" ]] || continue
            stop_client "$name"
      done
      if [[ -n "$TMP_DIR" && -d "$TMP_DIR" ]]; then
            rm -rf "$TMP_DIR"
      fi
}
trap cleanup EXIT INT TERM

send_line() {
      local name=$1
      local line=$2
      local input_fd=${CLIENT_IN[$name]}

      log "$name -> $line"
      if ! printf '%s\n' "$line" >&"$input_fd"; then
            fail "$name connection is not writable"
      fi
}

field_value() {
      local line=$1
      local wanted=$2
      local token

      for token in $line; do
            if [[ "$token" == "$wanted="* ]]; then
                  printf '%s' "${token#*=}"
                  return 0
            fi
      done
      return 1
}

next_action() {
      local name=$1
      local value=$((CLIENT_ACTION[$name] + 1))
      CLIENT_ACTION["$name"]=$value
      printf '%s' "$value"
}

pop_pending_match() {
      local name=$1
      local expression=$2
      local pending=${CLIENT_PENDING[$name]-}
      local remaining=""
      local line
      local matched=0

      [[ -n "$pending" ]] || return 1
      while IFS= read -r line; do
            [[ -n "$line" ]] || continue
            if ((matched == 0)) && [[ "$line" =~ $expression ]]; then
                  MATCH_LINE=$line
                  matched=1
            else
                  remaining+="$line"$'\n'
            fi
      done <<<"$pending"
      CLIENT_PENDING["$name"]=$remaining
      ((matched == 1))
}

# Wait for a line matching an extended regular expression. Profile chunk
# payloads have already been consumed and omitted by filter_protocol_output.
# Unmatched events are retained because one host operation can enqueue several
# state changes before the script starts waiting for the next one.
wait_for() {
      local name=$1
      local expression=$2
      local timeout_seconds=$3
      local input_fd=${CLIENT_IN[$name]}
      local output_fd=${CLIENT_OUT[$name]}
      local process_id=${CLIENT_PID[$name]}
      local line
      local partial=""
      local deadline=$((SECONDS + timeout_seconds))

      MATCH_LINE=""
      if pop_pending_match "$name" "$expression"; then
            return 0
      fi
      while ((SECONDS < deadline)); do
            line=""
            if IFS= read -r -t 0.01 line <&"$output_fd"; then
                  line="${partial}${line}"
                  partial=""
                  CLIENT_LAST_LINE["$name"]=$line
                  if [[ "$line" == PING\ * ]]; then
                        local ping_id=${line#*pingId=}
                        ping_id=${ping_id%% *}
                        printf 'PONG pingId=%s\n' "$ping_id" >&"$input_fd" ||
                              return 1
                        continue
                  fi
                  log "$name <- $line"
                  if [[ "$line" =~ $expression ]]; then
                        MATCH_LINE=$line
                        return 0
                  fi
                  CLIENT_PENDING["$name"]+="$line"$'\n'
            elif ! kill -0 "$process_id" 2>/dev/null; then
                  local error_file="$TMP_DIR/${name}.curl.stderr"
                  if [[ -s "$error_file" ]]; then
                        warn "$name curl: $(tr '\n' ' ' < "$error_file")"
                  fi
                  warn "$name curl process exited while waiting for: $expression"
                  return 1
            else
                  partial+=$line
                  # curl's telnet transport can stop polling the socket while
                  # stdin is idle after a large response. A harmless PONG wakes
                  # it so profile data continues draining from the host.
                  printf 'PONG pingId=0\n' >&"$input_fd" ||
                        return 1
            fi
      done
      warn "$name timed out waiting for: $expression"
      return 1
}

handshake_client() {
      local name=$1
      local nickname=$2
      local request_id="hello-${name}-${RANDOM}"
      local encoded_nickname
      local response
      local reconnect="0"

      encoded_nickname=$(urlencode "$nickname")
      send_line "$name" \
            "HELLO protocol=1 token=${CLIENT_TOKEN[$name]} nickname=$encoded_nickname packHash=$PACK_HASH profileTransfer=none lastSessionId=${CLIENT_SESSION[$name]} lastSnapshotSeq=0 requestId=$request_id"

      if ! wait_for "$name" '^((WELCOME)|(ERROR))[[:space:]]' "$WAIT_SECONDS"; then
            fail "$name did not complete the handshake"
      fi
      response=$MATCH_LINE
      if [[ "$response" == ERROR* ]]; then
            fail "$name handshake rejected: $response"
      fi

      CLIENT_ID["$name"]=$(field_value "$response" playerId) ||
            fail "$name WELCOME did not contain playerId"
      CLIENT_SESSION["$name"]=$(field_value "$response" sessionId) ||
            fail "$name WELCOME did not contain sessionId"
      reconnect=$(field_value "$response" reconnect || true)
      log "$name authenticated as ${CLIENT_ID[$name]} (reconnect=$reconnect)"

      send_line "$name" \
            "READY sessionId=${CLIENT_SESSION[$name]} playerId=${CLIENT_ID[$name]} requestId=ready-${name}-${RANDOM}"
      wait_for "$name" '^LOBBY_STATE[[:space:]]' "$WAIT_SECONDS" ||
            fail "$name did not receive lobby state"
}

check_protocol_error() {
      local name=$1
      local action
      local error_wait=$((WAIT_SECONDS * 4))
      ((error_wait >= 30)) || error_wait=30
      action=$(next_action "$name")
      send_line "$name" "UNKNOWN_COMMAND requestId=bad-${action}"
      wait_for "$name" '^ERROR[[:space:]].*requestId=bad-' "$error_wait" ||
            fail "$name did not receive an ERROR for an unknown command"
}

wait_for_game_start() {
      local name=$1
      if wait_for "$name" '^GAME_STARTED[[:space:]]' "$WAIT_SECONDS"; then
            return 0
      fi
      return 1
}

exercise_pause() {
      local name=$1
      local phase_line
      local phase_seq
      local action

      wait_for "$name" '^PHASE_START[[:space:]].*phase=PickingQuestion' \
            "$WAIT_SECONDS" || {
                  warn "cannot test pause: $name is not in question picking"
                  return 0
            }
      phase_line=$MATCH_LINE
      phase_seq=$(field_value "$phase_line" phaseSeq) ||
            fail "picking phase did not contain phaseSeq"
      action=$(next_action "$name")
      send_line "$name" \
            "PAUSE_REQUEST requestId=pause-$action actionId=$action phaseSeq=$phase_seq paused=1"
      wait_for "$name" '^PAUSE_STATE[[:space:]].*paused=1' "$WAIT_SECONDS" ||
            fail "pause was not acknowledged"
      action=$(next_action "$name")
      send_line "$name" \
            "PAUSE_REQUEST requestId=resume-$action actionId=$action phaseSeq=$phase_seq paused=0"
      wait_for "$name" '^PAUSE_STATE[[:space:]].*paused=0' "$WAIT_SECONDS" ||
            fail "resume was not acknowledged"
}

choose_question() {
      local picker_name=""
      local name
      local picker_line
      local picker_id
      local phase_seq
      local action

      # The host may choose itself. We inspect every remote client; if no
      # remote is the picker, the script reports that the host UI must select.
      for name in "${CLIENT_NAMES[@]}"; do
            if wait_for "$name" '^PICKER_CHANGED[[:space:]]' "$WAIT_SECONDS"; then
                  picker_line=$MATCH_LINE
                  picker_id=$(field_value "$picker_line" playerId) ||
                        fail "PICKER_CHANGED did not contain playerId"
                  if [[ "$picker_id" == "${CLIENT_ID[$name]}" ]]; then
                        picker_name=$name
                        phase_seq=$(field_value "$picker_line" phaseSeq) ||
                              fail "PICKER_CHANGED did not contain phaseSeq"
                        break
                  fi
            fi
      done

      if [[ -z "$picker_name" ]]; then
            warn "host is the picker, or picker event was not observed; remote clients cannot select"
            return 1
      fi

      action=$(next_action "$picker_name")
      send_line "$picker_name" \
            "SELECT_QUESTION requestId=pick-$action actionId=$action round=0 theme=$THEME_INDEX question=$QUESTION_INDEX phaseSeq=$phase_seq"
      log "$picker_name selected round 0, theme $THEME_INDEX, question $QUESTION_INDEX"
      return 0
}

exercise_reaction_and_answer() {
      local name
      local reaction_line=""
      local question_seq=""
      local reaction_phase_seq=""
      local winner_id=""
      local winner_name=""
      local answer_line
      local answer_phase_seq
      local action
      local elapsed=500
      local candidate

      for name in "${CLIENT_NAMES[@]}"; do
            wait_for "$name" '^QUESTION_START[[:space:]]' "$WAIT_SECONDS" ||
                  fail "$name did not receive QUESTION_START"
      done

      # A ForAll or SecretPublicPrice question intentionally has no normal
      # reaction event; the default smoke question is expected to be normal.
      if ! wait_for "${CLIENT_NAMES[0]}" '^REACTION_OPEN[[:space:]]' "$WAIT_SECONDS"; then
            warn "no normal reaction opened; this may be a ForAll/secret question or the host did not advance"
            return 0
      fi
      reaction_line=$MATCH_LINE
      question_seq=$(field_value "$reaction_line" questionSeq) ||
            fail "REACTION_OPEN did not contain questionSeq"
      reaction_phase_seq=$(field_value "$reaction_line" phaseSeq) ||
            fail "REACTION_OPEN did not contain phaseSeq"

      for name in "${CLIENT_NAMES[@]}"; do
            action=$(next_action "$name")
            send_line "$name" \
                  "REACTION_CLAIM requestId=react-$name-$action actionId=$action phaseSeq=$reaction_phase_seq questionSeq=$question_seq elapsedMs=$elapsed"
            elapsed=$((elapsed + 100))
      done

      if ! wait_for "${CLIENT_NAMES[0]}" \
            '^(REACTION_WINNER|ANSWER_REVEAL)[[:space:]]' "$WAIT_SECONDS"; then
            fail "host did not settle a reaction or reveal the answer"
      fi
      reaction_line=$MATCH_LINE
      if [[ "$reaction_line" == ANSWER_REVEAL* ]]; then
            log "answer revealed without a remote winner"
            return 0
      fi

      winner_id=$(field_value "$reaction_line" playerId) ||
            fail "REACTION_WINNER did not contain playerId"
      for candidate in "${CLIENT_NAMES[@]}"; do
            if [[ "${CLIENT_ID[$candidate]}" == "$winner_id" ]]; then
                  winner_name=$candidate
                  break
            fi
      done

      if [[ -z "$winner_name" ]]; then
            warn "host won the reaction; its local UI must answer"
            return 0
      fi

      if [[ "$SUBMIT_ANSWER" != 1 ]]; then
            log "winner is $winner_name; not submitting an answer (set SIO_SUBMIT_ANSWER=1 to test it)"
            return 0
      fi

      # ANSWER_OWNER carries the authoritative answer phase sequence. It is
      # broadcast, so reading it from the first client is sufficient.
      wait_for "${CLIENT_NAMES[0]}" '^ANSWER_OWNER[[:space:]]' "$WAIT_SECONDS" ||
            fail "winner was announced without ANSWER_OWNER"
      answer_line=$MATCH_LINE
      answer_phase_seq=$(field_value "$answer_line" phaseSeq) ||
            fail "ANSWER_OWNER did not contain phaseSeq"
      action=$(next_action "$winner_name")

      case "$ANSWER_TYPE" in
      Text)
            send_line "$winner_name" \
                  "ANSWER_SUBMIT requestId=answer-$action actionId=$action phaseSeq=$answer_phase_seq questionSeq=$question_seq answerType=Text answer=$(urlencode "$ANSWER_TEXT")"
            ;;
      Select)
            send_line "$winner_name" \
                  "ANSWER_SUBMIT requestId=answer-$action actionId=$action phaseSeq=$answer_phase_seq questionSeq=$question_seq answerType=Select optionId=$(urlencode "$SELECT_ID")"
            ;;
      Point)
            send_line "$winner_name" \
                  "ANSWER_SUBMIT requestId=answer-$action actionId=$action phaseSeq=$answer_phase_seq questionSeq=$question_seq answerType=Point x=$POINT_X y=$POINT_Y"
            ;;
      esac
      wait_for "${CLIENT_NAMES[0]}" \
            '^(ANSWER_RESULT|REACTION_RESUMED|ANSWER_REVEAL)[[:space:]]' "$WAIT_SECONDS" ||
            fail "host did not acknowledge the answer submission"
}

reconnect_client() {
      local name="p1"
      local saved_token=${CLIENT_TOKEN[$name]}
      local saved_session=${CLIENT_SESSION[$name]}

      log "stopping $name to test reconnect"
      stop_client "$name"
      CLIENT_TOKEN["$name"]=$saved_token
      CLIENT_SESSION["$name"]=$saved_session
      # start_client overwrites the descriptors but keeps the token/session
      # values only when supplied again.
      start_client "$name" "$saved_token"
      CLIENT_SESSION["$name"]=$saved_session
      handshake_client "$name" "Api-$name"
      wait_for "$name" '^SNAPSHOT_BEGIN[[:space:]]' "$WAIT_SECONDS" ||
            fail "reconnected client did not receive SNAPSHOT_BEGIN"
      wait_for "$name" '^SNAPSHOT_END[[:space:]]' "$WAIT_SECONDS" ||
            fail "reconnected client did not receive SNAPSHOT_END"
      log "$name reconnect snapshot completed"
}

check_host_reachable() {
      local error_file="$TMP_DIR/port-check.error"

      # Bash's TCP redirection gives a fast preflight without creating a
      # second curl client. The actual protocol test still uses curl below.
      if timeout "$CONNECT_TIMEOUT" bash -c \
            'exec 3<>"/dev/tcp/$1/$2"' _ "$HOST" "$PORT" \
            2>"$error_file"; then
            log "host is listening at ${HOST}:${PORT}"
            return 0
      fi

      fail "no multiplayer host is listening at ${HOST}:${PORT}; start the host application first (the script does not launch one)"
}

main() {
      local index
      local name
      local token
      local started=1

      parse_options "$@"
      require_command curl
      require_command awk
      require_command sed
      require_command timeout
      require_command find
      require_command stat
      require_command sha256sum
      validate_configuration

      local temp_root=${TMPDIR:-/tmp}
      if [[ ! -d "$temp_root" || ! -w "$temp_root" ]]; then
            temp_root=/tmp
      fi
      TMP_DIR=$(mktemp -d "$temp_root/sigame-api-test.XXXXXX")
      log "target: ${HOST}:${PORT}"
      log "pack hash: $PACK_HASH"
      log "virtual clients: $CLIENT_COUNT"
      check_host_reachable

      for ((index = 1; index <= CLIENT_COUNT; ++index)); do
            name="p$index"
            token=$(printf '00000000-0000-0000-0000-00000000000%d' "$index")
            start_client "$name" "$token"
            handshake_client "$name" "Api-$name"
      done

      if [[ "$TEST_ERRORS" == 1 ]]; then
            check_protocol_error "${CLIENT_NAMES[0]}"
      fi

      if [[ "$NO_ACTIONS" == 1 ]]; then
            log "handshake/lobby smoke test completed"
            return 0
      fi

      for name in "${CLIENT_NAMES[@]}"; do
            if ! wait_for_game_start "$name"; then
                  started=0
            fi
      done
      if ((started == 0)); then
            if [[ "$REQUIRE_GAME_START" == 1 ]]; then
                  fail "game did not start within $WAIT_SECONDS seconds"
            fi
            warn "lobby smoke test completed; host did not start a game"
            return 0
      fi
      log "game-start event received by every virtual client"

      if [[ "$TEST_RECONNECT" == 1 ]]; then
            reconnect_client
      fi

      if [[ "$TEST_PAUSE" == 1 ]]; then
            exercise_pause "${CLIENT_NAMES[0]}"
      fi

      if ! choose_question; then
            warn "stopping after game synchronization because host owns the picker"
            return 0
      fi

      exercise_reaction_and_answer
      log "multiplayer API smoke test completed"
}

main "$@"
