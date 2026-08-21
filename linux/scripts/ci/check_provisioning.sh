#!/usr/bin/env bash
# LD7 provisioning draft gate：只做 Ansible syntax/check；不 apply、不接板、不操作 systemd。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
PLAYBOOK="${ROOT}/deploy/provisioning/check.yml"
INVENTORY="${ROOT}/deploy/provisioning/inventory.example.yml"

[[ -f "${PLAYBOOK}" ]] || { echo "error: missing ${PLAYBOOK}" >&2; exit 1; }
[[ -f "${INVENTORY}" ]] || { echo "error: missing ${INVENTORY}" >&2; exit 1; }

if ! command -v ansible-playbook >/dev/null 2>&1; then
  echo "provisioning=unsupported reason=ansible-playbook-not-installed"
  exit 0
fi

ansible-playbook --syntax-check -i "${INVENTORY}" "${PLAYBOOK}"
if [[ -n "${RCR_ARTIFACT:-}" ]]; then
  [[ -f "${RCR_ARTIFACT}" ]] || { echo "error: RCR_ARTIFACT is not a file" >&2; exit 1; }
  RCR_ARTIFACT="${RCR_ARTIFACT}" ansible-playbook --check --diff \
    -i "${INVENTORY}" "${PLAYBOOK}"
  echo "provisioning=check_pass"
else
  echo "provisioning=syntax_pass check=not_run reason=RCR_ARTIFACT-not-set"
fi
