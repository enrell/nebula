#pragma once
#include <QStringList>

// `nebula ctl ...` client for the automation socket. Returns the process exit code.
int runCtl(const QStringList &args);

// `nebula FILE...`: the documents named on the command line (.neb, .md), as absolute paths. A word that is neither a
// flag nor a file is left alone (Qt options such as `-platform xcb` leave their value behind); a file that cannot be
// shown is reported on stderr and makes `ok` false.
QStringList documentArguments(const QStringList &args, bool *ok);

// Asks the running instance to open the documents as views. Returns the exit code.
int openDocuments(const QStringList &files);
