#pragma once
#include <QStringList>

// `nebula ctl ...` client for the automation socket. Returns the process exit code.
int runCtl(const QStringList &args);
