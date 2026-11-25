#pragma once
#include <QStringList>

// `nebula ctl ...` client for the automation socket. Returns the process exit code.
int runCtl(const QStringList &args);

// `nebula FILE...`: the files named on the command line, as absolute paths. .neb and Markdown files open as views,
// anything else in the editor (a new file is created on save). A word that is neither a flag nor a file is left
// alone (Qt options such as `-platform xcb` leave their value behind); a directory or a missing document is reported
// on stderr and makes `ok` false.
QStringList documentArguments(const QStringList &args, bool *ok);

// Asks the running instance to open the files. Returns the exit code.
int openDocuments(const QStringList &files);

// .neb and Markdown files open as views (rendered); everything else opens in the editor
bool isDocument(const QString &path);
