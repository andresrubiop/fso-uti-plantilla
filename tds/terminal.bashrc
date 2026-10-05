# Startup file of the shells opened by tds/terminal.py (bash --rcfile tds/terminal.bashrc).
# Your usual ~/.bashrc runs first (aliases, PATH...); then a short prompt, because the hub's terminal is small:
#   fso:td3$   = the current folder (tests/tmp/td3 when a lab's "Run" button took you there)
[ -f ~/.bashrc ] && . ~/.bashrc
PS1='\[\e[1;33m\]fso\[\e[0m\]:\[\e[1;34m\]\W\[\e[0m\]\$ '
