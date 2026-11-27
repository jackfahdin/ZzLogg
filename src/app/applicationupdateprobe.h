#pragma once

// Runs only for the exact --update-probe invocation, before KloggApp,
// installation activity, single-instance IPC or real user storage exists.
int runApplicationUpdateProbe( int argc, char* argv[] );
