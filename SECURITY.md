# Security model

This reference port executes trusted kernel extensions and trusted guest images with root and a crosvm configuration that disables its userspace sandbox. It is not an isolation product for hostile guests.

Do not post credentials, private disk images or sensitive runtime logs in an issue. For ordinary compatibility reports include only device model, Android/kernel release, BTF fingerprint, profile and a carefully reviewed error excerpt. Never include a token or private key.

ABI checks, resource bounds and normal teardown are part of the implementation, not optional performance switches. No support path should ask a user to force-load/unload a module, disable CFI, kill an uninterruptible VMM or flash a replacement Android kernel as a routine fix.
