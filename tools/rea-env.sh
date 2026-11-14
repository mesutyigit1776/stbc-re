# Source this file to set up the REA 6.5.0 and Ghidra 12.1.4 environment.
export JAVA_HOME=/opt/homebrew/opt/openjdk@21
export PATH="$JAVA_HOME/bin:$PATH"
export GHIDRA_INSTALL_DIR=/opt/homebrew/Cellar/ghidra/12.1.4/libexec
export REA_ANALYSIS_PROVIDER=ghidra
export REA_DIR=$HOME/Documents/rea-rea-agents-6.5.0
rea() { node "$REA_DIR/scripts/rea.mjs" "$@"; }
