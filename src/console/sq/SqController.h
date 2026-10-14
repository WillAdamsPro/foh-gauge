#pragma once
#include "SqAddressMap.h"
#include <fohcore/json.h>
#include <functional>
#include <map>
namespace foh::sq {
class SqController {
public:
    using Batch = std::vector<Bytes>;
    // The sink must accept the whole batch atomically, or return false.
    using Sink = std::function<bool(const Batch&)>;
    struct Request { Target target; double value; };
    struct Change { Target target; Bytes before, after; std::string description; };
    explicit SqController(Sink sink);
    void startConnection();
    void disconnect();
    void firmware(const Bytes& version);
    bool writesAllowed() const { return connected && firmwareAllowed; }
    const Bytes& versionBytes() const { return version; }
    bool echo(const Bytes& frame);
    std::optional<Frame> current(Target) const;
    bool propose(const std::vector<Request>&);
    bool proposeUndo();
    // Confirmation applies to this exact proposal; every edit/echo invalidates it.
    bool apply(bool confirmed, bool dryRun);
    void cancel() { proposal.clear(); }
    const std::vector<Change>& changes() const { return proposal; }
    const std::string& message() const { return status; }
    const Batch& dryRunFrames() const { return dryLog; }
    bool hasUndo() const { return !undo.empty(); }
    foh::Json sessionState() const;
    bool restoreSession(const foh::Json&);
private:
    friend struct SqControllerTestAccess; // offline test seam; no production bypass
    static unsigned key(Address);
    bool fail(const std::string&);
    bool addChange(Target, const Bytes&);
    Sink sink;
    bool connected = false, firmwareAllowed = false, undoProposal = false;
    Bytes version;
    std::map<unsigned, Bytes> cache;
    std::map<unsigned, std::uint16_t> pending;
    std::vector<Change> proposal;
    Batch undo, dryLog;
    std::vector<Batch> undoHistory;
    std::string status;
};
}
