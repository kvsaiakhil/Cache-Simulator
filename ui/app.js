function toHexLiteral(value) {
    return `0x${(value >>> 0).toString(16).toUpperCase()}`;
}

function generateSampleTrace(count) {
    if (count === 5) {
        return `
# Sample trace format:
# R <address>
# W <address> <value>

W 0x0 0x11223344
R 0x0
R 0x10
W 0x20 0xABCDEF01
R 0x20
`.trim();
    }

    const cycles = count / 5;
    const lines = [
        `# Sample trace format (${count === 1000 ? "medium" : "long"}):`,
        "# Repeated read/write locality.",
        "",
    ];
    for (let cycle = 0; cycle < cycles; cycle += 1) {
        const base = cycle * 0x40;
        lines.push(`W ${toHexLiteral(base)} ${toHexLiteral(((cycle + 1) * 0x1111) >>> 0)}`);
        lines.push(`R ${toHexLiteral(base)}`);
        lines.push(`R ${toHexLiteral(base + 0x10)}`);
        lines.push(`W ${toHexLiteral(base + 0x20)} ${toHexLiteral(((cycle + 1) * 0xABCDE) >>> 0)}`);
        lines.push(`R ${toHexLiteral(base + 0x20)}`);
    }
    return lines.join("\n");
}

function generateScanTrace(count) {
    if (count === 16) {
        return `
# SRRIP-inspired scan pattern:
# Two passes over a working set larger than the cache.

R 0x00
R 0x10
R 0x20
R 0x30
R 0x40
R 0x50
R 0x60
R 0x70
R 0x00
R 0x10
R 0x20
R 0x30
R 0x40
R 0x50
R 0x60
R 0x70
`.trim();
    }

    const passCount = count / 2;
    const lines = [
        `# SRRIP-inspired scan pattern (${count === 1000 ? "medium" : "long"}):`,
        "# Two passes over a working set larger than the cache.",
        "",
    ];
    for (let pass = 0; pass < 2; pass += 1) {
        for (let i = 0; i < passCount; i += 1) {
            lines.push(`R ${toHexLiteral(i * 0x10)}`);
        }
    }
    return lines.join("\n");
}

function generateThrashingTrace(count) {
    const lines = [
        count === 6 ? "# Conflict-thrashing pattern:" :
            `# Conflict-thrashing pattern (${count === 1000 ? "medium" : "long"}):`,
        "# Three blocks map to the same set in a 2-way cache.",
        "",
    ];
    const basePattern = ["R 0x00", "R 0x20", "R 0x40"];
    for (let i = 0; i < Math.floor(count / 3); i += 1) {
        lines.push(...basePattern);
    }
    for (let remainder = 0; remainder < (count % 3); remainder += 1) {
        lines.push(basePattern[remainder]);
    }
    return lines.join("\n");
}

function generateRecencyFriendlyTrace(count) {
    const lines = [
        count === 12 ? "# Recency-friendly pattern:" :
            `# Recency-friendly pattern (${count === 1000 ? "medium" : "long"}):`,
        "# Working set fits exactly in the cache and is reused often.",
        "",
    ];
    const basePattern = ["R 0x00", "R 0x10", "R 0x20", "R 0x30"];
    for (let i = 0; i < count / 4; i += 1) {
        lines.push(...basePattern);
    }
    return lines.join("\n");
}

function generateStreamingTrace(count) {
    const lines = [
        count === 12 ? "# Streaming pattern:" :
            `# Streaming pattern (${count === 1000 ? "medium" : "long"}):`,
        "# One pass over unique lines with no reuse.",
        "",
    ];
    for (let i = 0; i < count; i += 1) {
        lines.push(`R ${toHexLiteral(i * 0x10)}`);
    }
    return lines.join("\n");
}

function generateMixedTrace(count) {
    if (count === 13) {
        return `
# Mixed pattern:
# Hot blocks are repeatedly reused while a larger stream perturbs the cache.

R 0x00
R 0x10
R 0x20
R 0x30
R 0x00
R 0x10
R 0x40
R 0x00
R 0x10
R 0x20
R 0x30
R 0x00
R 0x10
`.trim();
    }

    const lines = [
        `# Mixed pattern (${count === 1000 ? "medium" : "long"}):`,
        "# Hot blocks are repeatedly reused while a larger stream perturbs the cache.",
        "",
    ];
    const basePattern = [
        "R 0x00",
        "R 0x10",
        "R 0x20",
        "R 0x30",
        "R 0x00",
        "R 0x10",
        "__STREAM__",
        "R 0x00",
        "R 0x10",
        "R 0x20",
        "R 0x30",
        "R 0x00",
        "R 0x10",
    ];
    const cycles = Math.floor(count / basePattern.length);
    let written = 0;
    for (let cycle = 0; cycle < cycles; cycle += 1) {
        const streamAddress = `R ${toHexLiteral(0x40 + cycle * 0x10)}`;
        for (const entry of basePattern) {
            lines.push(entry === "__STREAM__" ? streamAddress : entry);
            written += 1;
        }
    }
    let remainderIndex = 0;
    while (written < count) {
        const streamAddress = `R ${toHexLiteral(0x40 + cycles * 0x10)}`;
        const entry = basePattern[remainderIndex];
        lines.push(entry === "__STREAM__" ? streamAddress : entry);
        written += 1;
        remainderIndex += 1;
    }
    return lines.join("\n");
}

function buildTraceCatalog() {
    return {
        tiny: {
            sample_trace: generateSampleTrace(5),
            scan_trace: generateScanTrace(16),
            thrashing_trace: generateThrashingTrace(6),
            recency_friendly_trace: generateRecencyFriendlyTrace(12),
            streaming_trace: generateStreamingTrace(12),
            mixed_access_pattern_trace: generateMixedTrace(13),
        },
        medium: {
            sample_trace: generateSampleTrace(1000),
            scan_trace: generateScanTrace(1000),
            thrashing_trace: generateThrashingTrace(1000),
            recency_friendly_trace: generateRecencyFriendlyTrace(1000),
            streaming_trace: generateStreamingTrace(1000),
            mixed_access_pattern_trace: generateMixedTrace(1000),
        },
        long: {
            sample_trace: generateSampleTrace(10000),
            scan_trace: generateScanTrace(10000),
            thrashing_trace: generateThrashingTrace(10000),
            recency_friendly_trace: generateRecencyFriendlyTrace(10000),
            streaming_trace: generateStreamingTrace(10000),
            mixed_access_pattern_trace: generateMixedTrace(10000),
        },
    };
}

const BUILT_IN_TRACES = buildTraceCatalog();

const DEFAULT_CONFIG = {
    blockSizeBytes: 16,
    l1: { cacheSizeBytes: 64, ways: 2 },
    l2: { cacheSizeBytes: 128, ways: 2 },
    l3: { cacheSizeBytes: 256, ways: 4 },
};

const WRITE_MODES = {
    wb: {
        writePolicy: "write-back",
        writeMissPolicy: "write-allocate",
        shortLabel: "WB + WA",
    },
    wt: {
        writePolicy: "write-through",
        writeMissPolicy: "no-write-allocate",
        shortLabel: "WT + NWA",
    },
};

function parseVictimCacheSelection(value) {
    if (value === "off") {
        return { enabled: false, entries: 0, replacementPolicy: "lru", label: "Off" };
    }
    const [entriesToken, replacementPolicy = "lru"] = value.split(":");
    const entries = Number.parseInt(entriesToken, 10);
    return {
        enabled: true,
        entries,
        replacementPolicy,
        label: `${entries} • ${replacementPolicy.toUpperCase()}`,
    };
}

class CacheLevel {
    constructor(name, config, blockSizeBytes, replacementPolicy) {
        this.name = name;
        this.cacheSizeBytes = config.cacheSizeBytes;
        this.ways = config.ways;
        this.blockSizeBytes = blockSizeBytes;
        this.numBlocks = this.cacheSizeBytes / this.blockSizeBytes;
        this.numSets = this.numBlocks / this.ways;
        this.replacementPolicy = replacementPolicy;
        this.randomState = 0xC0FFEE ^ name.length;
        this.accessCounter = 0;
        this.stats = {
            readHits: 0,
            readMisses: 0,
            writeHits: 0,
            writeMisses: 0,
            writebacks: 0,
            compulsoryMisses: 0,
            capacityMisses: 0,
            conflictMisses: 0,
        };
        this.seenBlocks = new Set();
        this.shadow = [];

        this.sets = Array.from({ length: this.numSets }, () =>
            Array.from({ length: this.ways }, () => this.createEmptyLine()));
    }

    createEmptyLine() {
        return {
            valid: false,
            dirty: false,
            tag: 0,
            blockAddress: null,
            words: new Uint32Array(this.blockSizeBytes / 4),
            lastAccess: 0,
            insertionOrder: 0,
        };
    }

    cloneLine(line) {
        return {
            valid: line.valid,
            dirty: line.dirty,
            tag: line.tag,
            blockAddress: line.blockAddress,
            words: Uint32Array.from(line.words),
            lastAccess: line.lastAccess,
            insertionOrder: line.insertionOrder,
        };
    }

    blockAddress(byteAddress) {
        return Math.floor(byteAddress / this.blockSizeBytes);
    }

    setIndexForBlock(blockAddress) {
        return blockAddress % this.numSets;
    }

    tagForBlock(blockAddress) {
        return Math.floor(blockAddress / this.numSets);
    }

    wordIndex(byteAddress) {
        return Math.floor((byteAddress % this.blockSizeBytes) / 4);
    }

    touch(line, kind) {
        this.accessCounter += 1;
        line.lastAccess = this.accessCounter;
        if (line.insertionOrder === 0) {
            line.insertionOrder = this.accessCounter;
        }
        if (kind === "read") {
            this.stats.readHits += 1;
        } else if (kind === "write") {
            this.stats.writeHits += 1;
        }
    }

    miss(kind) {
        if (kind === "read") {
            this.stats.readMisses += 1;
        } else if (kind === "write") {
            this.stats.writeMisses += 1;
        }
    }

    findLine(byteAddress) {
        const blockAddress = this.blockAddress(byteAddress);
        const setIndex = this.setIndexForBlock(blockAddress);
        const tag = this.tagForBlock(blockAddress);
        const lines = this.sets[setIndex];
        const way = lines.findIndex((line) => line.valid && line.tag === tag);
        return { blockAddress, setIndex, tag, way, line: way >= 0 ? lines[way] : null };
    }

    containsLine(byteAddress) {
        return this.findLine(byteAddress).way >= 0;
    }

    readWord(byteAddress) {
        const hit = this.findLine(byteAddress);
        if (hit.way < 0) {
            this.miss("read");
            this.classifyMiss(hit.blockAddress);
            this.touchShadow(hit.blockAddress);
            return { hit: false, value: 0 };
        }
        this.touch(hit.line, "read");
        this.touchShadow(hit.blockAddress);
        return {
            hit: true,
            value: hit.line.words[this.wordIndex(byteAddress)],
            line: hit.line,
            setIndex: hit.setIndex,
            way: hit.way,
        };
    }

    writeWord(byteAddress, value, dirty) {
        const hit = this.findLine(byteAddress);
        if (hit.way < 0) {
            this.miss("write");
            this.classifyMiss(hit.blockAddress);
            this.touchShadow(hit.blockAddress);
            return { hit: false };
        }
        hit.line.words[this.wordIndex(byteAddress)] = value >>> 0;
        hit.line.dirty = dirty;
        this.touch(hit.line, "write");
        this.touchShadow(hit.blockAddress);
        return { hit: true, line: hit.line, setIndex: hit.setIndex, way: hit.way };
    }

    classifyMiss(blockAddress) {
        if (!this.seenBlocks.has(blockAddress)) {
            this.seenBlocks.add(blockAddress);
            this.stats.compulsoryMisses += 1;
            return;
        }

        if (this.shadow.includes(blockAddress)) {
            this.stats.conflictMisses += 1;
        } else {
            this.stats.capacityMisses += 1;
        }
    }

    touchShadow(blockAddress) {
        const index = this.shadow.indexOf(blockAddress);
        if (index >= 0) {
            this.shadow.splice(index, 1);
        }
        this.shadow.unshift(blockAddress);
        if (this.shadow.length > this.numBlocks) {
            this.shadow.pop();
        }
        this.seenBlocks.add(blockAddress);
    }

    getSnapshot(byteAddress) {
        const hit = this.findLine(byteAddress);
        return hit.way >= 0 ? this.cloneLine(hit.line) : null;
    }

    removeLine(byteAddress) {
        const hit = this.findLine(byteAddress);
        if (hit.way < 0) {
            return null;
        }
        const snapshot = this.cloneLine(hit.line);
        this.sets[hit.setIndex][hit.way] = this.createEmptyLine();
        return snapshot;
    }

    installLine(byteAddress, snapshot) {
        const blockAddress = this.blockAddress(byteAddress);
        const setIndex = this.setIndexForBlock(blockAddress);
        const tag = this.tagForBlock(blockAddress);
        const lines = this.sets[setIndex];

        this.accessCounter += 1;
        const existingWay = lines.findIndex((line) => line.valid && line.tag === tag);
        if (existingWay >= 0) {
            const updated = this.cloneLine(snapshot);
            updated.valid = true;
            updated.tag = tag;
            updated.blockAddress = blockAddress;
            updated.lastAccess = this.accessCounter;
            updated.insertionOrder = lines[existingWay].insertionOrder || this.accessCounter;
            lines[existingWay] = updated;
            return { eviction: null, setIndex, way: existingWay, line: updated };
        }

        const victimWay = this.chooseVictim(lines);
        const victim = lines[victimWay];
        const eviction = victim.valid ? this.cloneLine(victim) : null;
        if (eviction && eviction.dirty) {
            this.stats.writebacks += 1;
        }

        const installed = this.cloneLine(snapshot);
        installed.valid = true;
        installed.tag = tag;
        installed.blockAddress = blockAddress;
        installed.lastAccess = this.accessCounter;
        installed.insertionOrder = this.accessCounter;
        lines[victimWay] = installed;
        return { eviction, setIndex, way: victimWay, line: installed };
    }

    chooseVictim(lines) {
        const invalidWay = lines.findIndex((line) => !line.valid);
        if (invalidWay >= 0) {
            return invalidWay;
        }
        if (this.replacementPolicy === "fifo") {
            return lines.reduce((best, line, index) =>
                line.insertionOrder < lines[best].insertionOrder ? index : best, 0);
        }
        if (this.replacementPolicy === "random") {
            this.randomState = (1664525 * this.randomState + 1013904223) >>> 0;
            return this.randomState % lines.length;
        }
        return lines.reduce((best, line, index) =>
            line.lastAccess < lines[best].lastAccess ? index : best, 0);
    }

    toViewModel(activeBlockAddress) {
        return {
            name: this.name,
            cacheSizeBytes: this.cacheSizeBytes,
            ways: this.ways,
            numSets: this.numSets,
            stats: { ...this.stats },
            title: `${this.cacheSizeBytes}B • ${this.ways}-way`,
            meta: [
                `${this.numSets} sets`,
                `${this.blockSizeBytes}B blocks`,
                this.replacementPolicy.toUpperCase(),
            ],
            sets: this.sets.map((lines, setIndex) => ({
                setIndex,
                lines: lines.map((line, way) => ({
                    way,
                    valid: line.valid,
                    dirty: line.dirty,
                    tag: line.tag,
                    blockAddress: line.blockAddress,
                    words: Array.from(line.words),
                    active: line.valid && line.blockAddress === activeBlockAddress,
                })),
            })),
        };
    }
}

class VictimCache {
    constructor(config, blockSizeBytes) {
        this.blockSizeBytes = blockSizeBytes;
        this.enabledFlag = config.enabled;
        this.entriesCount = config.entries;
        this.replacementPolicy = config.replacementPolicy;
        this.randomState = 0xC0FFEE ^ 0x55AA;
        this.accessCounter = 0;
        this.stats = {
            readHits: 0,
            readMisses: 0,
            writeHits: 0,
            writeMisses: 0,
            writebacks: 0,
            compulsoryMisses: 0,
            capacityMisses: 0,
            conflictMisses: 0,
        };
        this.lines = Array.from({ length: this.entriesCount }, () => this.createEmptyLine());
    }

    enabled() {
        return this.enabledFlag;
    }

    entries() {
        return this.entriesCount;
    }

    createEmptyLine() {
        return {
            valid: false,
            dirty: false,
            blockAddress: null,
            words: new Uint32Array(this.blockSizeBytes / 4),
            lastAccess: 0,
            insertionOrder: 0,
        };
    }

    cloneLine(line) {
        return {
            valid: line.valid,
            dirty: line.dirty,
            blockAddress: line.blockAddress,
            words: Uint32Array.from(line.words),
            lastAccess: line.lastAccess,
            insertionOrder: line.insertionOrder,
        };
    }

    blockAddress(byteAddress) {
        return Math.floor(byteAddress / this.blockSizeBytes);
    }

    wordIndex(byteAddress) {
        return Math.floor((byteAddress % this.blockSizeBytes) / 4);
    }

    touch(line, kind) {
        this.accessCounter += 1;
        line.lastAccess = this.accessCounter;
        if (line.insertionOrder === 0) {
            line.insertionOrder = this.accessCounter;
        }
        if (kind === "read") {
            this.stats.readHits += 1;
        } else if (kind === "write") {
            this.stats.writeHits += 1;
        }
    }

    miss(kind) {
        if (kind === "read") {
            this.stats.readMisses += 1;
        } else if (kind === "write") {
            this.stats.writeMisses += 1;
        }
    }

    findLine(byteAddress) {
        const blockAddress = this.blockAddress(byteAddress);
        const way = this.lines.findIndex((line) => line.valid && line.blockAddress === blockAddress);
        return { blockAddress, way, line: way >= 0 ? this.lines[way] : null };
    }

    containsLine(byteAddress) {
        if (!this.enabled()) {
            return false;
        }
        return this.findLine(byteAddress).way >= 0;
    }

    accessLoad(byteAddress) {
        if (!this.enabled()) {
            return { hit: false, value: 0 };
        }
        const hit = this.findLine(byteAddress);
        if (hit.way < 0) {
            this.miss("read");
            return { hit: false, value: 0 };
        }
        this.touch(hit.line, "read");
        return { hit: true, value: hit.line.words[this.wordIndex(byteAddress)] };
    }

    accessStore(byteAddress, value, dirty) {
        if (!this.enabled()) {
            return { hit: false };
        }
        const hit = this.findLine(byteAddress);
        if (hit.way < 0) {
            this.miss("write");
            return { hit: false };
        }
        hit.line.words[this.wordIndex(byteAddress)] = value >>> 0;
        hit.line.dirty = dirty;
        this.touch(hit.line, "write");
        return { hit: true };
    }

    getSnapshot(byteAddress) {
        if (!this.enabled()) {
            return null;
        }
        const hit = this.findLine(byteAddress);
        return hit.way >= 0 ? this.cloneLine(hit.line) : null;
    }

    removeLine(byteAddress) {
        if (!this.enabled()) {
            return null;
        }
        const hit = this.findLine(byteAddress);
        if (hit.way < 0) {
            return null;
        }
        const snapshot = this.cloneLine(hit.line);
        this.lines[hit.way] = this.createEmptyLine();
        return snapshot;
    }

    installLine(byteAddress, snapshot) {
        if (!this.enabled()) {
            return { eviction: null };
        }
        const blockAddress = this.blockAddress(byteAddress);
        this.accessCounter += 1;
        const existingWay = this.lines.findIndex(
            (line) => line.valid && line.blockAddress === blockAddress
        );
        if (existingWay >= 0) {
            const updated = this.cloneLine(snapshot);
            updated.valid = true;
            updated.blockAddress = blockAddress;
            updated.lastAccess = this.accessCounter;
            updated.insertionOrder = this.lines[existingWay].insertionOrder || this.accessCounter;
            this.lines[existingWay] = updated;
            return { eviction: null, way: existingWay, line: updated };
        }

        const victimWay = this.chooseVictim();
        const victim = this.lines[victimWay];
        const eviction = victim.valid ? this.cloneLine(victim) : null;
        if (eviction && eviction.dirty) {
            this.stats.writebacks += 1;
        }

        const installed = this.cloneLine(snapshot);
        installed.valid = true;
        installed.blockAddress = blockAddress;
        installed.lastAccess = this.accessCounter;
        installed.insertionOrder = this.accessCounter;
        this.lines[victimWay] = installed;
        return { eviction, way: victimWay, line: installed };
    }

    chooseVictim() {
        const invalidWay = this.lines.findIndex((line) => !line.valid);
        if (invalidWay >= 0) {
            return invalidWay;
        }
        if (this.replacementPolicy === "fifo") {
            return this.lines.reduce(
                (best, line, index) =>
                    line.insertionOrder < this.lines[best].insertionOrder ? index : best,
                0
            );
        }
        if (this.replacementPolicy === "random") {
            this.randomState = (1664525 * this.randomState + 1013904223) >>> 0;
            return this.randomState % this.lines.length;
        }
        return this.lines.reduce(
            (best, line, index) => (line.lastAccess < this.lines[best].lastAccess ? index : best),
            0
        );
    }

    toViewModel(activeBlockAddress) {
        return {
            name: "VC",
            cacheSizeBytes: this.entriesCount * this.blockSizeBytes,
            ways: this.entriesCount,
            numSets: 1,
            stats: { ...this.stats },
            title: `${this.entriesCount} entries • fully associative`,
            meta: [
                "1 set",
                `${this.blockSizeBytes}B blocks`,
                this.replacementPolicy.toUpperCase(),
            ],
            sets: [{
                setIndex: 0,
                lines: this.lines.map((line, way) => ({
                    way,
                    valid: line.valid,
                    dirty: line.dirty,
                    tag: line.blockAddress ?? 0,
                    blockAddress: line.blockAddress,
                    words: Array.from(line.words),
                    active: line.valid && line.blockAddress === activeBlockAddress,
                })),
            }],
        };
    }
}

class CacheHierarchySimulator {
    constructor({ inclusionMode, writeMode, replacementPolicy, victimCache }) {
        this.blockSizeBytes = DEFAULT_CONFIG.blockSizeBytes;
        this.replacementPolicy = replacementPolicy;
        this.inclusionMode = inclusionMode;
        this.writeMode = writeMode;
        this.levels = {
            l1: new CacheLevel("L1", DEFAULT_CONFIG.l1, this.blockSizeBytes, replacementPolicy),
            l2: new CacheLevel("L2", DEFAULT_CONFIG.l2, this.blockSizeBytes, replacementPolicy),
            l3: new CacheLevel("L3", DEFAULT_CONFIG.l3, this.blockSizeBytes, replacementPolicy),
        };
        this.victimCache = new VictimCache(victimCache, this.blockSizeBytes);
        this.memory = new Map();
        this.summary = {
            operations: 0,
            loads: 0,
            stores: 0,
        };
    }

    createZeroLine(blockAddress) {
        const wordsPerBlock = this.blockSizeBytes / 4;
        return {
            valid: true,
            dirty: false,
            tag: 0,
            blockAddress,
            words: new Uint32Array(wordsPerBlock),
            lastAccess: 0,
            insertionOrder: 0,
        };
    }

    cloneLine(line) {
        return {
            valid: line.valid,
            dirty: line.dirty,
            tag: line.tag,
            blockAddress: line.blockAddress,
            words: Uint32Array.from(line.words),
            lastAccess: line.lastAccess,
            insertionOrder: line.insertionOrder,
        };
    }

    blockAddress(byteAddress) {
        return Math.floor(byteAddress / this.blockSizeBytes);
    }

    wordIndex(byteAddress) {
        return Math.floor((byteAddress % this.blockSizeBytes) / 4);
    }

    loadMemoryLine(blockAddress) {
        const line = this.memory.get(blockAddress);
        return this.cloneLine(line || this.createZeroLine(blockAddress));
    }

    storeMemoryLine(blockAddress, snapshot) {
        const stored = this.cloneLine(snapshot);
        stored.dirty = false;
        this.memory.set(blockAddress, stored);
    }

    applyOperation(operation) {
        this.summary.operations += 1;
        const blockAddress = this.blockAddress(operation.address);
        const wordIndex = this.wordIndex(operation.address);

        if (operation.type === "R") {
            this.summary.loads += 1;
            return this.load(operation.address, blockAddress, wordIndex);
        }
        this.summary.stores += 1;
        return this.store(operation.address, blockAddress, wordIndex, operation.value);
    }

    load(byteAddress, blockAddress, wordIndex) {
        const l1Result = this.levels.l1.readWord(byteAddress);
        if (l1Result.hit) {
            return this.composeResult({
                operationType: "R",
                address: byteAddress,
                value: l1Result.value,
                hit: true,
                resultLevel: "L1",
                activeBlockAddress: blockAddress,
                path: ["L1 hit"],
            });
        }

        const victimResult = this.victimCache.accessLoad(byteAddress);
        if (victimResult.hit) {
            const snapshot = this.takeFromVictimOrThrow(byteAddress);
            this.insertIntoL1Cluster(byteAddress, snapshot);
            return this.composeResult({
                operationType: "R",
                address: byteAddress,
                value: this.levels.l1.getSnapshot(byteAddress).words[wordIndex],
                hit: false,
                resultLevel: "VC",
                activeBlockAddress: blockAddress,
                path: ["L1 miss", "VC hit", "Promoted upward"],
            });
        }

        const l2Result = this.levels.l2.readWord(byteAddress);
        if (l2Result.hit) {
            const snapshot = this.extractOrClone("l2", byteAddress);
            this.fillUpperLevels(byteAddress, snapshot, 2);
            return this.composeResult({
                operationType: "R",
                address: byteAddress,
                value: this.levels.l1.getSnapshot(byteAddress).words[wordIndex],
                hit: false,
                resultLevel: "L2",
                activeBlockAddress: blockAddress,
                path: ["L1 miss", "L2 hit", "Promoted upward"],
            });
        }

        const l3Result = this.levels.l3.readWord(byteAddress);
        if (l3Result.hit) {
            const snapshot = this.extractOrClone("l3", byteAddress);
            this.fillUpperLevels(byteAddress, snapshot, 3);
            return this.composeResult({
                operationType: "R",
                address: byteAddress,
                value: this.levels.l1.getSnapshot(byteAddress).words[wordIndex],
                hit: false,
                resultLevel: "L3",
                activeBlockAddress: blockAddress,
                path: ["L1 miss", "L2 miss", "L3 hit", "Promoted upward"],
            });
        }

        const snapshot = this.loadMemoryLine(blockAddress);
        this.fillUpperLevels(byteAddress, snapshot, 0);
        return this.composeResult({
            operationType: "R",
            address: byteAddress,
            value: this.levels.l1.getSnapshot(byteAddress).words[wordIndex],
            hit: false,
            resultLevel: "MEM",
            activeBlockAddress: blockAddress,
            path: ["L1 miss", "L2 miss", "L3 miss", "Fetched from memory"],
        });
    }

    store(byteAddress, blockAddress, wordIndex, value) {
        if (this.writeMode === "wb") {
            return this.storeWriteBack(byteAddress, blockAddress, wordIndex, value);
        }
        return this.storeWriteThrough(byteAddress, blockAddress, wordIndex, value);
    }

    storeWriteBack(byteAddress, blockAddress, wordIndex, value) {
        const l1Hit = this.levels.l1.writeWord(byteAddress, value, true);
        if (l1Hit.hit) {
            return this.composeResult({
                operationType: "W",
                address: byteAddress,
                value,
                hit: true,
                resultLevel: "L1",
                activeBlockAddress: blockAddress,
                path: ["L1 write hit", "Marked dirty"],
            });
        }

        const victimHit = this.victimCache.accessStore(byteAddress, value, true);
        if (victimHit.hit) {
            const snapshot = this.takeFromVictimOrThrow(byteAddress);
            this.insertIntoL1Cluster(byteAddress, snapshot);
            return this.composeResult({
                operationType: "W",
                address: byteAddress,
                value,
                hit: false,
                resultLevel: "VC",
                activeBlockAddress: blockAddress,
                path: ["L1 write miss", "VC hit", "Promoted upward"],
            });
        }

        let snapshot = null;
        let sourceLevel = 0;
        if (this.levels.l2.containsLine(byteAddress)) {
            this.levels.l2.stats.readHits += 1;
            snapshot = this.extractOrClone("l2", byteAddress);
            sourceLevel = 2;
        } else {
            this.levels.l2.stats.readMisses += 1;
        }

        if (!snapshot) {
            if (this.levels.l3.containsLine(byteAddress)) {
                this.levels.l3.stats.readHits += 1;
                snapshot = this.extractOrClone("l3", byteAddress);
                sourceLevel = 3;
            } else {
                this.levels.l3.stats.readMisses += 1;
            }
        }

        if (!snapshot) {
            snapshot = this.loadMemoryLine(blockAddress);
        }

        snapshot.words[wordIndex] = value >>> 0;
        snapshot.dirty = true;
        this.fillUpperLevels(byteAddress, snapshot, sourceLevel);

        return this.composeResult({
            operationType: "W",
            address: byteAddress,
            value,
            hit: false,
            resultLevel: sourceLevel === 0 ? "MEM" : `L${sourceLevel}`,
            activeBlockAddress: blockAddress,
            path: [
                "L1 write miss",
                sourceLevel === 2 ? "Refilled from L2" :
                    sourceLevel === 3 ? "Refilled from L3" : "Allocated from memory",
                "Stored in hierarchy",
            ],
        });
    }

    storeWriteThrough(byteAddress, blockAddress, wordIndex, value) {
        const l1Hit = this.levels.l1.writeWord(byteAddress, value, false);
        if (l1Hit.hit) {
            this.propagateWriteThroughHit(byteAddress, value);
            return this.composeResult({
                operationType: "W",
                address: byteAddress,
                value,
                hit: true,
                resultLevel: "L1",
                activeBlockAddress: blockAddress,
                path: ["L1 write hit", "Propagated downward", "Committed to memory"],
            });
        }

        const victimHit = this.victimCache.accessStore(byteAddress, value, false);
        if (victimHit.hit) {
            const snapshot = this.takeFromVictimOrThrow(byteAddress);
            this.insertIntoL1Cluster(byteAddress, snapshot);
            this.propagateWriteThroughHit(byteAddress, value);
            return this.composeResult({
                operationType: "W",
                address: byteAddress,
                value,
                hit: false,
                resultLevel: "VC",
                activeBlockAddress: blockAddress,
                path: ["L1 write miss", "VC hit", "Promoted upward", "Committed to memory"],
            });
        }

        let updatedLevel = "MEM";
        if (this.levels.l2.containsLine(byteAddress)) {
            this.levels.l2.writeWord(byteAddress, value, false);
            updatedLevel = "L2";
            if (this.inclusionMode !== "exclusive" && this.levels.l3.containsLine(byteAddress)) {
                this.levels.l3.writeWord(byteAddress, value, false);
            }
        } else if (this.levels.l3.containsLine(byteAddress)) {
            this.levels.l3.writeWord(byteAddress, value, false);
            updatedLevel = "L3";
        }
        this.writeThroughToMemory(blockAddress, wordIndex, value);
        return this.composeResult({
            operationType: "W",
            address: byteAddress,
            value,
            hit: false,
            resultLevel: updatedLevel,
            activeBlockAddress: blockAddress,
            path: ["L1 write miss", "No allocation", `Updated ${updatedLevel}`, "Committed to memory"],
        });
    }

    extractOrClone(levelName, byteAddress) {
        if (this.inclusionMode === "exclusive") {
            return this.levels[levelName].removeLine(byteAddress);
        }
        return this.levels[levelName].getSnapshot(byteAddress);
    }

    fillUpperLevels(byteAddress, snapshot, sourceLevel) {
        if (this.inclusionMode === "inclusive") {
            if (sourceLevel === 0) {
                this.insertIntoLevel("l3", byteAddress, snapshot);
            }
            if (sourceLevel === 0 || sourceLevel === 3) {
                this.insertIntoLevel("l2", byteAddress, snapshot);
            }
            this.insertIntoL1Cluster(byteAddress, snapshot);
            return;
        }

        if (this.inclusionMode === "non-inclusive") {
            if (sourceLevel === 0) {
                this.insertIntoLevel("l3", byteAddress, snapshot);
            }
            if (sourceLevel === 0 || sourceLevel === 3) {
                this.insertIntoLevel("l2", byteAddress, snapshot);
            }
            this.insertIntoL1Cluster(byteAddress, snapshot);
            return;
        }

        this.insertIntoL1Cluster(byteAddress, snapshot);
    }

    insertIntoL1Cluster(byteAddress, snapshot) {
        const result = this.levels.l1.installLine(byteAddress, snapshot);
        if (!result.eviction) {
            return;
        }

        if (this.victimCache.enabled()) {
            const victimResult = this.victimCache.installLine(
                result.eviction.blockAddress * this.blockSizeBytes,
                result.eviction
            );
            if (victimResult.eviction) {
                this.handleTopClusterEviction(victimResult.eviction.blockAddress, victimResult.eviction);
            }
            return;
        }

        this.handleTopClusterEviction(result.eviction.blockAddress, result.eviction);
    }

    handleTopClusterEviction(evictedBlockAddress, snapshot) {
        if (this.inclusionMode === "exclusive") {
            this.demoteTopClusterLine(evictedBlockAddress, snapshot);
            return;
        }

        if (snapshot.dirty) {
            this.insertIntoLevel("l2", evictedBlockAddress * this.blockSizeBytes, snapshot);
        }
    }

    demoteTopClusterLine(evictedBlockAddress, snapshot) {
        const l2Result = this.levels.l2.installLine(evictedBlockAddress * this.blockSizeBytes, snapshot);
        if (!l2Result.eviction) {
            return;
        }
        const l3Result = this.levels.l3.installLine(
            l2Result.eviction.blockAddress * this.blockSizeBytes,
            l2Result.eviction
        );
        if (l3Result.eviction && l3Result.eviction.dirty) {
            this.storeMemoryLine(l3Result.eviction.blockAddress, l3Result.eviction);
        }
    }

    invalidateUpperClusterLine(byteAddress) {
        this.levels.l1.removeLine(byteAddress);
        if (this.victimCache.enabled()) {
            this.victimCache.removeLine(byteAddress);
        }
    }

    takeFromVictimOrThrow(byteAddress) {
        const snapshot = this.victimCache.removeLine(byteAddress);
        if (!snapshot) {
            throw new Error("Victim cache line disappeared unexpectedly");
        }
        return snapshot;
    }

    overwriteWithDirtySnapshotIfPresent(cacheLike, byteAddress, snapshot) {
        const candidate = cacheLike.getSnapshot(byteAddress);
        if (!candidate || !candidate.dirty) {
            return snapshot;
        }
        return candidate;
    }

    reconcileInclusiveEvictionSnapshot(levelName, byteAddress, snapshot) {
        let reconciled = snapshot;
        if (levelName === "l3") {
            reconciled = this.overwriteWithDirtySnapshotIfPresent(this.levels.l2, byteAddress, reconciled);
            if (this.victimCache.enabled()) {
                reconciled = this.overwriteWithDirtySnapshotIfPresent(this.victimCache, byteAddress, reconciled);
            }
            reconciled = this.overwriteWithDirtySnapshotIfPresent(this.levels.l1, byteAddress, reconciled);
            return reconciled;
        }
        if (levelName === "l2") {
            if (this.victimCache.enabled()) {
                reconciled = this.overwriteWithDirtySnapshotIfPresent(this.victimCache, byteAddress, reconciled);
            }
            reconciled = this.overwriteWithDirtySnapshotIfPresent(this.levels.l1, byteAddress, reconciled);
        }
        return reconciled;
    }

    insertIntoLevel(levelName, byteAddress, snapshot) {
        const level = this.levels[levelName];
        const result = level.installLine(byteAddress, snapshot);
        if (!result.eviction) {
            return;
        }

        let evicted = result.eviction;
        if (this.inclusionMode === "inclusive") {
            evicted = this.reconcileInclusiveEvictionSnapshot(
                levelName,
                evicted.blockAddress * this.blockSizeBytes,
                evicted
            );
            if (levelName === "l3") {
                this.levels.l2.removeLine(evicted.blockAddress * this.blockSizeBytes);
                this.invalidateUpperClusterLine(evicted.blockAddress * this.blockSizeBytes);
            } else if (levelName === "l2") {
                this.invalidateUpperClusterLine(evicted.blockAddress * this.blockSizeBytes);
            }
            if (evicted.dirty) {
                this.propagateDirtyEviction(levelName, evicted);
            }
            return;
        }

        if (this.inclusionMode === "non-inclusive") {
            if (evicted.dirty) {
                this.propagateDirtyEviction(levelName, evicted);
            }
            return;
        }

        if (levelName === "l1") {
            const downResult = this.levels.l2.installLine(evicted.blockAddress * this.blockSizeBytes, evicted);
            if (downResult.eviction) {
                const finalResult = this.levels.l3.installLine(
                    downResult.eviction.blockAddress * this.blockSizeBytes,
                    downResult.eviction
                );
                if (finalResult.eviction && finalResult.eviction.dirty) {
                    this.storeMemoryLine(finalResult.eviction.blockAddress, finalResult.eviction);
                }
            }
        } else if (levelName === "l2") {
            const downResult = this.levels.l3.installLine(evicted.blockAddress * this.blockSizeBytes, evicted);
            if (downResult.eviction && downResult.eviction.dirty) {
                this.storeMemoryLine(downResult.eviction.blockAddress, downResult.eviction);
            }
        } else if (evicted.dirty) {
            this.storeMemoryLine(evicted.blockAddress, evicted);
        }
    }

    propagateDirtyEviction(levelName, evicted) {
        if (levelName === "l1") {
            this.insertIntoLevel("l2", evicted.blockAddress * this.blockSizeBytes, evicted);
            return;
        }
        if (levelName === "l2") {
            this.insertIntoLevel("l3", evicted.blockAddress * this.blockSizeBytes, evicted);
            return;
        }
        this.storeMemoryLine(evicted.blockAddress, evicted);
    }

    propagateWriteThroughHit(byteAddress, value) {
        if (this.inclusionMode === "inclusive" && !this.levels.l2.containsLine(byteAddress)) {
            throw new Error("Inclusive hierarchy expected resident line in L2");
        }
        if (this.inclusionMode === "inclusive" && !this.levels.l3.containsLine(byteAddress)) {
            throw new Error("Inclusive hierarchy expected resident line in L3");
        }
        if (this.levels.l2.containsLine(byteAddress)) {
            const l2Hit = this.levels.l2.writeWord(byteAddress, value, false);
            if (!l2Hit.hit) {
                throw new Error("Hierarchy expected resident line in L2");
            }
        }
        if (this.levels.l3.containsLine(byteAddress)) {
            const l3Hit = this.levels.l3.writeWord(byteAddress, value, false);
            if (!l3Hit.hit) {
                throw new Error("Hierarchy expected resident line in L3");
            }
        }
        this.writeThroughToMemory(this.blockAddress(byteAddress), this.wordIndex(byteAddress), value);
    }

    writeThroughToMemory(blockAddress, wordIndex, value) {
        const snapshot = this.loadMemoryLine(blockAddress);
        snapshot.words[wordIndex] = value >>> 0;
        snapshot.dirty = false;
        this.storeMemoryLine(blockAddress, snapshot);
    }

    composeResult({ operationType, address, value, hit, resultLevel, activeBlockAddress, path }) {
        const l1Stats = this.levels.l1.stats;
        const totalAccesses =
            l1Stats.readHits + l1Stats.readMisses + l1Stats.writeHits + l1Stats.writeMisses;
        const totalHits = l1Stats.readHits + l1Stats.writeHits;
        const statsRows = [["L1", this.levels.l1.stats]];
        if (this.victimCache.enabled()) {
            statsRows.push(["VC", this.victimCache.stats]);
        }
        statsRows.push(["L2", this.levels.l2.stats], ["L3", this.levels.l3.stats]);

        return {
            operationType,
            address,
            value,
            hit,
            resultLevel,
            activeBlockAddress,
            path,
            summary: {
                ...this.summary,
                l1HitRate: totalAccesses === 0 ? 0 : (100 * totalHits) / totalAccesses,
            },
            missBreakdown: {
                compulsory: l1Stats.compulsoryMisses,
                capacity: l1Stats.capacityMisses,
                conflict: l1Stats.conflictMisses,
            },
            levels: {
                l1: this.levels.l1.toViewModel(activeBlockAddress),
                vc: this.victimCache.enabled() ? this.victimCache.toViewModel(activeBlockAddress) : null,
                l2: this.levels.l2.toViewModel(activeBlockAddress),
                l3: this.levels.l3.toViewModel(activeBlockAddress),
            },
            exports: {
                csv: [
                    "level,read_hits,read_misses,write_hits,write_misses,writebacks,compulsory_misses,capacity_misses,conflict_misses",
                    ...statsRows.map(([name, stats]) =>
                        `${name},${stats.readHits},${stats.readMisses},${stats.writeHits},${stats.writeMisses},${stats.writebacks},${stats.compulsoryMisses},${stats.capacityMisses},${stats.conflictMisses}`
                    ),
                ].join("\n"),
                json: JSON.stringify(Object.fromEntries(
                    statsRows.map(([name, stats]) => [name, {
                        read_hits: stats.readHits,
                        read_misses: stats.readMisses,
                        write_hits: stats.writeHits,
                        write_misses: stats.writeMisses,
                        writebacks: stats.writebacks,
                        compulsory_misses: stats.compulsoryMisses,
                        capacity_misses: stats.capacityMisses,
                        conflict_misses: stats.conflictMisses,
                    }])
                ), null, 2),
            },
        };
    }
}

function parseTrace(text) {
    return text
        .split("\n")
        .map((line, index) => ({ line: line.trim(), number: index + 1 }))
        .filter(({ line }) => line.length > 0)
        .map(({ line, number }) => {
            const normalized = line.split("#")[0].trim();
            if (!normalized) {
                return null;
            }
            const parts = normalized.split(/\s+/);
            const type = parts[0];
            if (type !== "R" && type !== "W") {
                throw new Error(`Invalid operation on line ${number}`);
            }
            if (type === "R" && parts.length !== 2) {
                throw new Error(`Read line ${number} must be: R <address>`);
            }
            if (type === "W" && parts.length !== 3) {
                throw new Error(`Write line ${number} must be: W <address> <value>`);
            }
            const address = parseNumeric(parts[1], number);
            if (address % 4 !== 0) {
                throw new Error(`Address on line ${number} must be 4-byte aligned`);
            }
            if (type === "R") {
                return { type, address, lineNumber: number };
            }
            return {
                type,
                address,
                value: parseNumeric(parts[2], number),
                lineNumber: number,
            };
        })
        .filter(Boolean);
}

function parseNumeric(token, lineNumber) {
    if (token.startsWith("-")) {
        throw new Error(`Negative numeric token on line ${lineNumber}`);
    }
    const value = Number(token);
    if (!Number.isInteger(value) || value < 0 || value > 0xFFFFFFFF) {
        throw new Error(`Invalid 32-bit numeric token on line ${lineNumber}: ${token}`);
    }
    return value >>> 0;
}

function formatHex(value, width = 8) {
    return `0x${(value >>> 0).toString(16).toUpperCase().padStart(width, "0")}`;
}

const state = {
    operations: [],
    currentIndex: 0,
    simulator: null,
    timerId: null,
    lastFrame: null,
};

const els = {
    traceSizeSelect: document.getElementById("trace-size-select"),
    traceSelect: document.getElementById("trace-select"),
    traceInput: document.getElementById("trace-input"),
    inclusionSelect: document.getElementById("inclusion-select"),
    writeModeSelect: document.getElementById("write-mode-select"),
    replacementSelect: document.getElementById("replacement-select"),
    victimCacheSelect: document.getElementById("victim-cache-select"),
    loadTraceBtn: document.getElementById("load-trace-btn"),
    resetBtn: document.getElementById("reset-btn"),
    stepBtn: document.getElementById("step-btn"),
    playBtn: document.getElementById("play-btn"),
    pauseBtn: document.getElementById("pause-btn"),
    speedRange: document.getElementById("speed-range"),
    speedLabel: document.getElementById("speed-label"),
    activeTraceName: document.getElementById("active-trace-name"),
    currentOpLabel: document.getElementById("current-op-label"),
    activeHierarchyLabel: document.getElementById("active-hierarchy-label"),
    activeWriteLabel: document.getElementById("active-write-label"),
    activeVictimLabel: document.getElementById("active-victim-label"),
    progressLabel: document.getElementById("progress-label"),
    lastResultLabel: document.getElementById("last-result-label"),
    currentValueLabel: document.getElementById("current-value-label"),
    metricOps: document.getElementById("metric-ops"),
    metricLoads: document.getElementById("metric-loads"),
    metricStores: document.getElementById("metric-stores"),
    metricHitRate: document.getElementById("metric-hit-rate"),
    missBreakdown: document.getElementById("miss-breakdown"),
    eventLog: document.getElementById("event-log"),
    levelsRoot: document.getElementById("levels-root"),
    levelTemplate: document.getElementById("level-template"),
    csvOutput: document.getElementById("csv-output"),
    jsonOutput: document.getElementById("json-output"),
};

function init() {
    els.traceSizeSelect.value = "tiny";
    refreshTracePresetOptions();
    els.traceSelect.value = "sample_trace";
    loadSelectedPreset(false);
    els.speedLabel.textContent = `${els.speedRange.value} ms`;

    els.traceSizeSelect.addEventListener("change", () => {
        refreshTracePresetOptions();
        els.traceSelect.value = "sample_trace";
        loadSelectedPreset();
    });

    els.traceSelect.addEventListener("change", () => {
        loadSelectedPreset();
    });

    els.speedRange.addEventListener("input", () => {
        els.speedLabel.textContent = `${els.speedRange.value} ms`;
        if (state.timerId !== null) {
            stopPlayback();
            startPlayback();
        }
    });

    els.loadTraceBtn.addEventListener("click", loadTrace);
    els.resetBtn.addEventListener("click", resetSimulation);
    els.stepBtn.addEventListener("click", stepForward);
    els.playBtn.addEventListener("click", startPlayback);
    els.pauseBtn.addEventListener("click", stopPlayback);

    els.inclusionSelect.addEventListener("change", () => {
        syncPolicyLabels();
        resetSimulation();
    });
    els.writeModeSelect.addEventListener("change", () => {
        syncPolicyLabels();
        resetSimulation();
    });
    els.replacementSelect.addEventListener("change", resetSimulation);
    els.victimCacheSelect.addEventListener("change", () => {
        syncPolicyLabels();
        resetSimulation();
    });

    syncPolicyLabels();
    loadTrace();
}

function refreshTracePresetOptions() {
    const selectedSize = els.traceSizeSelect.value;
    els.traceSelect.innerHTML = "";
    Object.keys(BUILT_IN_TRACES[selectedSize]).forEach((name) => {
        const option = document.createElement("option");
        option.value = name;
        option.textContent = name.replaceAll("_", " ");
        els.traceSelect.appendChild(option);
    });
}

function loadSelectedPreset(shouldLoad = true) {
    const selectedSize = els.traceSizeSelect.value;
    const selectedTrace = els.traceSelect.value;
    els.traceInput.value = BUILT_IN_TRACES[selectedSize][selectedTrace];
    els.activeTraceName.textContent = `${selectedSize}/${selectedTrace}`;
    if (shouldLoad) {
        loadTrace();
    }
}

function syncPolicyLabels() {
    const inclusionLabelMap = {
        inclusive: "Inclusive",
        exclusive: "Exclusive",
        "non-inclusive": "Non-inclusive",
    };
    els.activeHierarchyLabel.textContent = inclusionLabelMap[els.inclusionSelect.value];
    els.activeWriteLabel.textContent = WRITE_MODES[els.writeModeSelect.value].shortLabel;
    els.activeVictimLabel.textContent = parseVictimCacheSelection(els.victimCacheSelect.value).label;
}

function buildSimulator() {
    return new CacheHierarchySimulator({
        inclusionMode: els.inclusionSelect.value,
        writeMode: els.writeModeSelect.value,
        replacementPolicy: els.replacementSelect.value,
        victimCache: parseVictimCacheSelection(els.victimCacheSelect.value),
    });
}

function loadTrace() {
    stopPlayback();
    try {
        state.operations = parseTrace(els.traceInput.value);
        state.currentIndex = 0;
        state.simulator = buildSimulator();
        state.lastFrame = state.simulator.composeResult({
            operationType: "-",
            address: 0,
            value: 0,
            hit: false,
            resultLevel: "Idle",
            activeBlockAddress: null,
            path: ["Trace loaded"],
        });
        els.eventLog.innerHTML = "";
        appendEvent({
            operationType: "Trace",
            address: 0,
            value: 0,
            hit: false,
            resultLevel: "Ready",
            path: [`Loaded ${state.operations.length} operations`],
        });
        render(state.lastFrame);
    } catch (error) {
        stopPlayback();
        alert(error.message);
    }
}

function resetSimulation() {
    loadTrace();
}

function stepForward() {
    if (state.currentIndex >= state.operations.length || !state.simulator) {
        stopPlayback();
        return;
    }
    const op = state.operations[state.currentIndex];
    state.lastFrame = state.simulator.applyOperation(op);
    state.currentIndex += 1;
    render(state.lastFrame, op);
    appendEvent({
        operationType: op.type,
        address: op.address,
        value: op.type === "W" ? op.value : state.lastFrame.value,
        hit: state.lastFrame.hit,
        resultLevel: state.lastFrame.resultLevel,
        path: state.lastFrame.path,
    });
    if (state.currentIndex >= state.operations.length) {
        stopPlayback();
    }
}

function startPlayback() {
    if (state.timerId !== null) {
        return;
    }
    if (state.currentIndex >= state.operations.length) {
        resetSimulation();
    }
    state.timerId = window.setInterval(stepForward, Number(els.speedRange.value));
}

function stopPlayback() {
    if (state.timerId !== null) {
        window.clearInterval(state.timerId);
        state.timerId = null;
    }
}

function appendEvent(event) {
    const item = document.createElement("article");
    item.className = "event-item";
    const resultClass = event.hit ? "hit" : "miss";
    const resultLabel = event.hit ? "hit" : "miss";
    item.innerHTML = `
        <header>
            <strong>${event.operationType} ${event.operationType === "Trace" ? "" : formatHex(event.address)}</strong>
            <strong class="${resultClass}">${event.resultLevel} ${resultLabel}</strong>
        </header>
        <p>${event.path.join(" • ")}</p>
    `;
    els.eventLog.prepend(item);
    while (els.eventLog.children.length > 18) {
        els.eventLog.removeChild(els.eventLog.lastChild);
    }
}

function render(frame, operation = null) {
    els.progressLabel.textContent = `${state.currentIndex} / ${state.operations.length}`;
    els.currentOpLabel.textContent = operation
        ? `${operation.type} ${formatHex(operation.address)}`
        : "Idle";
    els.lastResultLabel.textContent = `${frame.resultLevel} ${frame.hit ? "hit" : "miss"}`;
    els.currentValueLabel.textContent =
        operation && operation.type === "R" ? formatHex(frame.value) :
        operation && operation.type === "W" ? formatHex(operation.value) : "-";

    els.metricOps.textContent = String(frame.summary.operations);
    els.metricLoads.textContent = String(frame.summary.loads);
    els.metricStores.textContent = String(frame.summary.stores);
    els.metricHitRate.textContent = `${frame.summary.l1HitRate.toFixed(1)}%`;

    els.missBreakdown.innerHTML = "";
    [
        ["Compulsory", frame.missBreakdown.compulsory],
        ["Capacity", frame.missBreakdown.capacity],
        ["Conflict", frame.missBreakdown.conflict],
    ].forEach(([label, value]) => {
        const tile = document.createElement("article");
        tile.className = "miss-tile";
        tile.innerHTML = `<span class="label">${label}</span><strong>${value}</strong>`;
        els.missBreakdown.appendChild(tile);
    });

    els.levelsRoot.innerHTML = "";
    ["l1", "vc", "l2", "l3"].forEach((key) => {
        if (frame.levels[key]) {
            renderLevel(frame.levels[key]);
        }
    });
    els.csvOutput.textContent = frame.exports.csv;
    els.jsonOutput.textContent = frame.exports.json;
}

function renderLevel(level) {
    const fragment = els.levelTemplate.content.cloneNode(true);
    fragment.querySelector(".level-name").textContent = level.name;
    fragment.querySelector(".level-title").textContent = level.title;

    const meta = fragment.querySelector(".level-meta");
    level.meta.forEach((text) => {
        const pill = document.createElement("span");
        pill.textContent = text;
        meta.appendChild(pill);
    });

    const statRoot = fragment.querySelector(".level-stats");
    [
        `RH ${level.stats.readHits}`,
        `RM ${level.stats.readMisses}`,
        `WH ${level.stats.writeHits}`,
        `WM ${level.stats.writeMisses}`,
        `WB ${level.stats.writebacks}`,
        `CM ${level.stats.compulsoryMisses}`,
        `Cap ${level.stats.capacityMisses}`,
        `Conf ${level.stats.conflictMisses}`,
    ].forEach((text) => {
        const chip = document.createElement("span");
        chip.className = "chip";
        chip.textContent = text;
        statRoot.appendChild(chip);
    });

    const setGrid = fragment.querySelector(".set-grid");
    level.sets.forEach((set) => {
        const setCard = document.createElement("section");
        setCard.className = "set-card";
        const setHead = document.createElement("div");
        setHead.className = "set-head";
        setHead.innerHTML = `<strong>Set ${set.setIndex}</strong><span class="label">${set.lines.length} ways</span>`;
        setCard.appendChild(setHead);

        const ways = document.createElement("div");
        ways.className = "ways";
        set.lines.forEach((line) => ways.appendChild(renderLine(line)));
        setCard.appendChild(ways);
        setGrid.appendChild(setCard);
    });

    els.levelsRoot.appendChild(fragment);
}

function renderLine(line) {
    const lineCard = document.createElement("article");
    lineCard.className = `line-card${line.active ? " active" : ""}${line.valid ? "" : " invalid"}`;

    const chips = line.valid
        ? `
            <span class="way-pill">Tag ${line.tag}</span>
            <span class="way-pill">Block ${line.blockAddress}</span>
            <span class="way-pill">${line.dirty ? "Dirty" : "Clean"}</span>
        `
        : `<span class="way-pill">Empty</span>`;

    const words = line.valid
        ? line.words.map((word, index) => `
            <div class="word-tile">
                <span>Word ${index}</span>
                <strong>${formatHex(word)}</strong>
            </div>
        `).join("")
        : "";

    lineCard.innerHTML = `
        <div class="line-head">
            <strong>Way ${line.way}</strong>
            <span class="label">${line.valid ? "Resident" : "Invalid"}</span>
        </div>
        <div class="line-chips">${chips}</div>
        <div class="word-grid">${words}</div>
    `;
    return lineCard;
}

init();
