#pragma once
#include <QJsonObject>
#include <QList>
#include <QString>
#include <functional>

// The tool surface shared by the MCP bridge (used by external agents) and the built-in operator.
// Every tool is a thin wrapper over the nebula automation API, reached through an injected `Api` callable.
namespace Tools {

using Api = std::function<QJsonValue(const QString &method, const QJsonObject &params)>;

struct ToolError { QString msg; };

struct Spec {
    QString name, description;
    QJsonObject schema;
    bool mutating = false;      // changes the workspace or talks to a pane
    bool destructive = false;   // needs explicit user approval in the operator
    bool mcpOnly = false;       // blocking / environment-specific, not offered to the operator
    std::function<QString(const QJsonObject &args, const Api &api)> run;
};

const QList<Spec> &all();
const Spec *find(const QString &name);
bool needsApproval(const Spec &s, const QJsonObject &args);   // destructive tools and dangerous run_action values
QString pretty(const QJsonValue &v);

} // namespace Tools
