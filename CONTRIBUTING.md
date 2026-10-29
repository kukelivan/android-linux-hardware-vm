# Contributing

Keep changes device-explicit. A new device port needs an audited ABI, toolchain/config provenance, guest IPA layout and normal teardown evidence; changing a fingerprint alone is not a port.

Do not submit personal images, credentials, VPN configuration, runtime logs with sensitive data, or proprietary application source. Use generic examples. Preserve upstream notices and document kernel/private-ABI dependencies.

Run `python3 scripts/check-source.py`, compile Python modules, build ARM64 shims/agent, and inspect host helper initialization before loading a changed module. Do not bypass CFI, SG limits, reserve guards or reference counts to make a launch succeed. Explain how cleanup works when an operation fails part-way through.
