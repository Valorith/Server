  const DATA_URL = "./data/rof2-reference.json";
  const LIVE_SESSION_URL = "./data/live-session.json";
  const API_SESSION_URL = "./api/session";
  const API_INTERFACES_URL = "./api/interfaces";
  const API_OPEN_CAPTURES_URL = "./api/open-captures-folder";
  const API_RESCAN_OPCODES_URL = "./api/rescan-opcodes";
const STORAGE_KEY = "occ-rof2-state-v1";
const STATUS_OPTIONS = [
  "unreviewed",
  "watching",
  "suspected",
  "validated",
  "sheet-known",
  "needs-capture",
  "ignored",
];
const STATUS_LABELS = {
  unreviewed: "Unreviewed",
  watching: "Watching",
  suspected: "Suspected",
  validated: "Confirmed",
  "sheet-known": "Reference Known",
  "patch-known": "Confirmed",
  "needs-capture": "Needs Capture",
  ignored: "Ignored",
};
const SOURCE_LABELS = {
  sheet: "Reference",
  "sheet+patch": "Reference + EQEmu",
  "patch-only": "EQEmu Only",
  custom: "Custom",
};
const ALIGNMENT_LABELS = {
  match: "Match",
  mismatch: "Mismatch",
  "sheet-only": "Reference Only",
  "patch-only-name": "EQEmu Only",
  unnamed: "Unnamed",
  "patch-only": "EQEmu Only",
  custom: "Custom",
};
const SORT_DEFAULT_DIRECTIONS = {
  tracked: "desc",
  recent: "desc",
  alert: "desc",
  count: "desc",
  opcode: "asc",
  name: "asc",
  eqemu: "asc",
  status: "asc",
  source: "asc",
  notes: "asc",
};
const LIVE_MONITOR_RENDER_LIMIT = 90;
const WORKFLOW_STAGES = ["candidate", "repeatable", "isolated", "opcode-hypothesis", "confirmed"];
const WORKFLOW_STAGE_LABELS = {
  candidate: "Candidate Packet",
  repeatable: "Repeatable Trigger",
  isolated: "Isolated Capture",
  "opcode-hypothesis": "Opcode Hypothesis",
  confirmed: "Confirmed Mapping",
};

const state = {
  data: null,
  customEntries: [],
  entryState: {},
  bookmarkState: {},
  opcodePreferences: {},
  packetPreferences: {},
  suppressionRules: {},
  selectedId: null,
  selectedBookmarkId: null,
  inspectorOpen: false,
  workflowModalOpen: false,
  bookmarkModalOpen: false,
  liveMonitorOpen: false,
  renameOpcodeModalOpen: false,
  filters: { search: "", status: "all", source: "all", sort: "tracked", sortDirection: "desc", trackedOnly: false },
  pagination: { page: 1, pageSize: 50 },
  bookmarkPagination: { page: 1, pageSize: 6 },
  liveMonitorFilters: { tab: "feed", mode: "all", search: "", unknownOnly: false, countLimit: "" },
  liveMonitorFeedBaseline: { sessionKey: "", frameNumber: 0, timeEpoch: 0 },
  liveSession: { status: "idle", markers: [], sessionName: "", markerCount: 0, activityCount: 0, activity: [], detectionCount: 0, detections: [] },
  seenDetectionIds: new Set(),
  seenActivityIds: new Set(),
  recentActivityIds: new Set(),
  recentDetectedEntryIds: new Set(),
  recentCountEntryIds: new Set(),
  recentActivityTimers: {},
  recentDetectionTimers: {},
  recentCountTimers: {},
  currentAlertMatches: [],
  currentSessionKey: "",
  countPulseSessionKey: "",
  lastSessionEntryCounts: new Map(),
  detectionBootstrapComplete: false,
  audioContext: null,
  sessionActionPending: false,
  registryRescanPending: false,
  sessionFormInitialized: false,
  sessionNameSeed: "",
  interfaces: [],
  ignoreCustomEntryOutsideClickUntil: 0,
  customEntryOverlayMode: false,
  liveMonitorContextMenu: { open: false, detectionId: "", opcode: "", groupKey: "", x: 0, y: 0 },
  renameOpcodeTarget: "",
  renamePacketTarget: "",
};

const els = {
  statsGrid: document.querySelector("#statsGrid"),
  toggleCustomEntryButton: document.querySelector("#toggleCustomEntryButton"),
  customEntryDrawer: document.querySelector("#customEntryDrawer"),
  toggleSettingsButton: document.querySelector("#toggleSettingsButton"),
  settingsDrawer: document.querySelector("#settingsDrawer"),
  searchInput: document.querySelector("#searchInput"),
  statusFilter: document.querySelector("#statusFilter"),
  sourceFilter: document.querySelector("#sourceFilter"),
  sortFilter: document.querySelector("#sortFilter"),
  sortButtons: [...document.querySelectorAll(".sort-button")],
  trackedOnlyToggle: document.querySelector("#trackedOnlyToggle"),
  clearFiltersButton: document.querySelector("#clearFiltersButton"),
  resultsSummary: document.querySelector("#resultsSummary"),
  registryActionMessage: document.querySelector("#registryActionMessage"),
  selectionSummary: document.querySelector("#selectionSummary"),
  sessionStatusBadge: document.querySelector("#sessionStatusBadge"),
  refreshSessionButton: document.querySelector("#refreshSessionButton"),
  sessionNameValue: document.querySelector("#sessionNameValue"),
  sessionMarkersValue: document.querySelector("#sessionMarkersValue"),
  sessionInterfaceValue: document.querySelector("#sessionInterfaceValue"),
  sessionFilterValue: document.querySelector("#sessionFilterValue"),
  sessionDetectionsValue: document.querySelector("#sessionDetectionsValue"),
  sessionNameInput: document.querySelector("#sessionNameInput"),
  sessionInterfaceInput: document.querySelector("#sessionInterfaceInput"),
  sessionCaptureFilterInput: document.querySelector("#sessionCaptureFilterInput"),
  sessionDurationInput: document.querySelector("#sessionDurationInput"),
  sessionToggleButton: document.querySelector("#sessionToggleButton"),
  restartSessionButton: document.querySelector("#restartSessionButton"),
  openLiveMonitorButton: document.querySelector("#openLiveMonitorButton"),
  openCapturesFolderButton: document.querySelector("#openCapturesFolderButton"),
  sessionMarkerLabelInput: document.querySelector("#sessionMarkerLabelInput"),
  sessionMarkerNoteInput: document.querySelector("#sessionMarkerNoteInput"),
  markSessionButton: document.querySelector("#markSessionButton"),
  sessionActionMessage: document.querySelector("#sessionActionMessage"),
  sessionTimingValue: document.querySelector("#sessionTimingValue"),
  sessionPathsValue: document.querySelector("#sessionPathsValue"),
  bookmarkEmpty: document.querySelector("#bookmarkEmpty"),
  bookmarkWorkspace: document.querySelector("#bookmarkWorkspace"),
  bookmarkList: document.querySelector("#bookmarkList"),
  prevBookmarkPageButton: document.querySelector("#prevBookmarkPageButton"),
  nextBookmarkPageButton: document.querySelector("#nextBookmarkPageButton"),
  bookmarkPageSummary: document.querySelector("#bookmarkPageSummary"),
  bookmarkModal: document.querySelector("#bookmarkModal"),
  closeBookmarkModalButton: document.querySelector("#closeBookmarkModalButton"),
  bookmarkTitle: document.querySelector("#bookmarkModalTitle"),
  bookmarkTime: document.querySelector("#bookmarkTime"),
  bookmarkSourceNote: document.querySelector("#bookmarkSourceNote"),
  bookmarkExpectedOpcode: document.querySelector("#bookmarkExpectedOpcode"),
  bookmarkLinkedEntry: document.querySelector("#bookmarkLinkedEntry"),
  linkSelectedOpcodeButton: document.querySelector("#linkSelectedOpcodeButton"),
  bookmarkNotes: document.querySelector("#bookmarkNotes"),
  resetMarkersButton: document.querySelector("#resetMarkersButton"),
  prevPageButton: document.querySelector("#prevPageButton"),
  nextPageButton: document.querySelector("#nextPageButton"),
  pageSummary: document.querySelector("#pageSummary"),
  pageSizeSelect: document.querySelector("#pageSizeSelect"),
  opcodeTableBody: document.querySelector("#opcodeTableBody"),
  rowTemplate: document.querySelector("#rowTemplate"),
  customOpcode: document.querySelector("#customOpcode"),
  customName: document.querySelector("#customName"),
  customPacketSignature: document.querySelector("#customPacketSignature"),
  customPacketFamilyKey: document.querySelector("#customPacketFamilyKey"),
  customConfidence: document.querySelector("#customConfidence"),
  customNotes: document.querySelector("#customNotes"),
  addCustomEntryButton: document.querySelector("#addCustomEntryButton"),
  exportStateButton: document.querySelector("#exportStateButton"),
  importStateInput: document.querySelector("#importStateInput"),
  resetStateButton: document.querySelector("#resetStateButton"),
  rescanOpcodesButton: document.querySelector("#rescanOpcodesButton"),
  inspectorModal: document.querySelector("#inspectorModal"),
  closeInspectorButton: document.querySelector("#closeInspectorButton"),
  inspectorContent: document.querySelector("#inspectorContent"),
  inspectorTitle: document.querySelector("#inspectorTitle"),
  inspectorSourceLine: document.querySelector("#inspectorSourceLine"),
  inspectorAlignment: document.querySelector("#inspectorAlignment"),
  inspectorTrackedState: document.querySelector("#inspectorTrackedState"),
  inspectorLastTouched: document.querySelector("#inspectorLastTouched"),
  inspectorOpcode: document.querySelector("#inspectorOpcode"),
  inspectorSheetName: document.querySelector("#inspectorSheetName"),
  inspectorEqemuName: document.querySelector("#inspectorEqemuName"),
  inspectorTestOpcode: document.querySelector("#inspectorTestOpcode"),
  entryStatus: document.querySelector("#entryStatus"),
  entryConfidence: document.querySelector("#entryConfidence"),
  entryAlertEnabled: document.querySelector("#entryAlertEnabled"),
  inspectorWorkflowStageSummary: document.querySelector("#inspectorWorkflowStageSummary"),
  inspectorWorkflowProgressPill: document.querySelector("#inspectorWorkflowProgressPill"),
  openWorkflowButton: document.querySelector("#openWorkflowButton"),
  workflowModal: document.querySelector("#workflowModal"),
  closeWorkflowButton: document.querySelector("#closeWorkflowButton"),
  workflowModalTitle: document.querySelector("#workflowModalTitle"),
  workflowEntryOpcode: document.querySelector("#workflowEntryOpcode"),
  workflowEntryName: document.querySelector("#workflowEntryName"),
  workflowStepper: document.querySelector(".workflow-stepper"),
  entryWorkflowStage: document.querySelector("#entryWorkflowStage"),
  workflowProgressPill: document.querySelector("#workflowProgressPill"),
  workflowSessionCount: document.querySelector("#workflowSessionCount"),
  workflowMarkerCount: document.querySelector("#workflowMarkerCount"),
  workflowIdentityValue: document.querySelector("#workflowIdentityValue"),
  workflowAlertValue: document.querySelector("#workflowAlertValue"),
  workflowRepeatableCheck: document.querySelector("#workflowRepeatableCheck"),
  workflowIsolatedCheck: document.querySelector("#workflowIsolatedCheck"),
  workflowHypothesisCheck: document.querySelector("#workflowHypothesisCheck"),
  workflowConfirmedCheck: document.querySelector("#workflowConfirmedCheck"),
  workflowSyncStageButton: document.querySelector("#workflowSyncStageButton"),
  workflowApplyHypothesisButton: document.querySelector("#workflowApplyHypothesisButton"),
  workflowPromoteHypothesisButton: document.querySelector("#workflowPromoteHypothesisButton"),
  workflowAnalysisSummary: document.querySelector("#workflowAnalysisSummary"),
  workflowNextStep: document.querySelector("#workflowNextStep"),
  workflowReferenceHints: document.querySelector("#workflowReferenceHints"),
  workflowBackButton: document.querySelector("#workflowBackButton"),
  workflowAdvanceButton: document.querySelector("#workflowAdvanceButton"),
  entryTags: document.querySelector("#entryTags"),
  entryNotes: document.querySelector("#entryNotes"),
  entryReferenceNotes: document.querySelector("#entryReferenceNotes"),
  copySummaryButton: document.querySelector("#copySummaryButton"),
  deleteCustomEntryButton: document.querySelector("#deleteCustomEntryButton"),
  prevEntryButton: document.querySelector("#prevEntryButton"),
  nextEntryButton: document.querySelector("#nextEntryButton"),
  detectionAlertModal: document.querySelector("#detectionAlertModal"),
  detectionAlertSummary: document.querySelector("#detectionAlertSummary"),
  detectionAlertList: document.querySelector("#detectionAlertList"),
  dismissDetectionAlertButton: document.querySelector("#dismissDetectionAlertButton"),
  dismissDetectionAlertFooterButton: document.querySelector("#dismissDetectionAlertFooterButton"),
  openDetectionEntryButton: document.querySelector("#openDetectionEntryButton"),
  liveMonitorModal: document.querySelector("#liveMonitorModal"),
  closeLiveMonitorButton: document.querySelector("#closeLiveMonitorButton"),
  liveMonitorEyebrow: document.querySelector("#liveMonitorEyebrow"),
  liveMonitorSummary: document.querySelector("#liveMonitorSummary"),
  liveFeedTabButton: document.querySelector("#liveFeedTabButton"),
  suppressedTabButton: document.querySelector("#suppressedTabButton"),
  clearLiveMonitorFeedButton: document.querySelector("#clearLiveMonitorFeedButton"),
  liveMonitorModeFilter: document.querySelector("#liveMonitorModeFilter"),
  liveMonitorSearchInput: document.querySelector("#liveMonitorSearchInput"),
  liveMonitorCountLimitInput: document.querySelector("#liveMonitorCountLimitInput"),
  liveMonitorUnknownOnlyToggle: document.querySelector("#liveMonitorUnknownOnlyToggle"),
  liveMonitorPacketsValue: document.querySelector("#liveMonitorPacketsValue"),
  liveMonitorCandidatesValue: document.querySelector("#liveMonitorCandidatesValue"),
  liveMonitorFlaggedValue: document.querySelector("#liveMonitorFlaggedValue"),
  liveMonitorSyncValue: document.querySelector("#liveMonitorSyncValue"),
  liveMonitorEmpty: document.querySelector("#liveMonitorEmpty"),
  liveMonitorList: document.querySelector("#liveMonitorList"),
  suppressedListEmpty: document.querySelector("#suppressedListEmpty"),
  suppressedList: document.querySelector("#suppressedList"),
  liveMonitorContextMenu: document.querySelector("#liveMonitorContextMenu"),
  liveMonitorContextTitle: document.querySelector("#liveMonitorContextTitle"),
  toggleLiveMonitorFlagButton: document.querySelector("#toggleLiveMonitorFlagButton"),
  renameLiveMonitorOpcodeButton: document.querySelector("#renameLiveMonitorOpcodeButton"),
  createLiveMonitorEntryButton: document.querySelector("#createLiveMonitorEntryButton"),
  suppressLiveMonitorItemButton: document.querySelector("#suppressLiveMonitorItemButton"),
  openLiveMonitorEntryButton: document.querySelector("#openLiveMonitorEntryButton"),
  renameOpcodeModal: document.querySelector("#renameOpcodeModal"),
  closeRenameOpcodeButton: document.querySelector("#closeRenameOpcodeButton"),
  renameOpcodeTitle: document.querySelector("#renameOpcodeTitle"),
  renameOpcodeSummary: document.querySelector("#renameOpcodeSummary"),
  renameOpcodeInput: document.querySelector("#renameOpcodeInput"),
  saveRenameOpcodeButton: document.querySelector("#saveRenameOpcodeButton"),
  clearRenameOpcodeButton: document.querySelector("#clearRenameOpcodeButton"),
};

const runtimeCache = {
  entryVersion: 0,
  preferenceVersion: 0,
  suppressionVersion: 0,
  allEntriesVersion: -1,
  allEntries: [],
  entryById: new Map(),
  entriesByOpcode: new Map(),
  entriesByPacketSignature: new Map(),
  entriesByPacketFamilyKey: new Map(),
  flaggedEntriesByOpcode: new Map(),
  flaggedEntriesByPacketSignature: new Map(),
  flaggedEntriesByPacketFamilyKey: new Map(),
  flaggedCustomEntries: [],
  suppressionRulesVersion: -1,
  suppressionRules: [],
  activityVersionKey: "",
  activityRef: null,
  activityEntryVersion: -1,
  activityPreferenceVersion: -1,
  activitySuppressionVersion: -1,
  activityEntries: [],
  activityById: new Map(),
  sessionEntryCountVersionKey: "",
  sessionEntryCountActivityRef: null,
  sessionEntryCountDetectionsRef: null,
  sessionEntryCountEntryVersion: -1,
  sessionEntryCountPreferenceVersion: -1,
  sessionEntryCounts: new Map(),
};

function invalidateEntryCaches() {
  runtimeCache.entryVersion += 1;
  runtimeCache.allEntriesVersion = -1;
  runtimeCache.allEntries = [];
  runtimeCache.entryById = new Map();
  runtimeCache.entriesByOpcode = new Map();
  runtimeCache.entriesByPacketSignature = new Map();
  runtimeCache.entriesByPacketFamilyKey = new Map();
  runtimeCache.flaggedEntriesByOpcode = new Map();
  runtimeCache.flaggedEntriesByPacketSignature = new Map();
  runtimeCache.flaggedEntriesByPacketFamilyKey = new Map();
  runtimeCache.flaggedCustomEntries = [];
  runtimeCache.activityVersionKey = "";
  runtimeCache.activityEntries = [];
  runtimeCache.activityById = new Map();
}

function invalidatePreferenceCaches() {
  runtimeCache.preferenceVersion += 1;
  runtimeCache.activityVersionKey = "";
  runtimeCache.activityEntries = [];
  runtimeCache.activityById = new Map();
}

function invalidateSuppressionCaches() {
  runtimeCache.suppressionVersion += 1;
  runtimeCache.suppressionRulesVersion = -1;
  runtimeCache.suppressionRules = [];
  runtimeCache.activityVersionKey = "";
  runtimeCache.activityEntries = [];
  runtimeCache.activityById = new Map();
}

function normalizeOpcode(value) {
  const raw = (value || "").trim().toLowerCase();
  if (!raw) return "";
  const clean = raw.startsWith("0x") ? raw.slice(2) : raw;
  if (!/^[0-9a-f]{1,4}$/.test(clean)) return "";
  return `0x${parseInt(clean, 16).toString(16).padStart(4, "0")}`;
}

function normalizePacketSignature(value) {
  const raw = String(value || "").trim().toLowerCase();
  if (!raw) return "";
  const clean = raw.replace(/[^0-9a-f]/g, "");
  return clean.length ? clean : "";
}

function normalizeLabelKey(value) {
  return String(value || "").toLowerCase().replace(/[^a-z0-9]+/g, "");
}

function normalizePacketFamilyKey(value) {
  return String(value || "").trim();
}

function getWorkflowStageIndex(stage) {
  const index = WORKFLOW_STAGES.indexOf(stage);
  return index === -1 ? 0 : index;
}

function normalizeWorkflowStage(stage) {
  return WORKFLOW_STAGES.includes(stage) ? stage : "candidate";
}

function getOpcodePreference(opcode) {
  const normalized = normalizeOpcode(opcode);
  if (!normalized) return { opcode: "", alias: "", flagged: false };
  const saved = state.opcodePreferences[normalized] || {};
  return {
    opcode: normalized,
    alias: saved.alias || "",
    flagged: Boolean(saved.flagged),
  };
}

function getPacketPreferenceKey(item, options = {}) {
  const groupKey = String(options.groupKey || item?.groupKey || "").trim();
  if (groupKey) return `group:${groupKey}`;
  const signature = buildPacketSuppressionSignature(item);
  return signature ? `packet:${signature}` : "";
}

function getPacketPreference(item, options = {}) {
  const key = getPacketPreferenceKey(item, options);
  const saved = key ? state.packetPreferences[key] || {} : {};
  return {
    key,
    alias: saved.alias || "",
  };
}

function normalizeSuppressionText(value) {
  return normalizeMonitorText(value || "").trim().toLowerCase();
}

function buildPacketSuppressionSignature(item) {
  const payloadPrefix = String(item?.payloadPrefix || "").slice(0, 64).toLowerCase();
  const info = normalizeSuppressionText(item?.info || "");
  return [
    item?.src || "",
    item?.srcport || "",
    item?.dst || "",
    item?.dstport || "",
    info,
    payloadPrefix,
  ].join("|");
}

function buildSuppressionRule(item, options = {}) {
  const opcode = normalizeOpcode(item?.opcode);
  const route = `${item?.src || "?"}:${item?.srcport || "?"} -> ${item?.dst || "?"}:${item?.dstport || "?"}`;
  const sampleInfo = normalizeMonitorText(item?.info || "No tshark info");
  const samplePayloadPrefix = item?.payloadPrefix || "";
  const sampleLength = item?.length || "";
  const sampleFrameNumber = item?.frameNumber || "";
  const sampleDetectedUtc = item?.detectedUtc || "";
  const sampleAnalysisSource = item?.analysisSource || "";
  const groupKey = String(options.groupKey || item?.groupKey || "").trim();
  if (opcode) {
    return {
      key: `opcode:${opcode}`,
      type: "opcode",
      opcode,
      label: getDetectionDisplayName(item),
      matcherLabel: opcode,
      sampleRoute: route,
      sampleInfo,
      samplePayloadPrefix,
      sampleLength,
      sampleFrameNumber,
      sampleDetectedUtc,
      sampleAnalysisSource,
      createdAt: new Date().toISOString(),
    };
  }

  if (groupKey) {
    return {
      key: `group:${groupKey}`,
      type: "group",
      groupKey,
      label: getDetectionDisplayName(item),
      matcherLabel: route,
      sampleRoute: route,
      sampleInfo,
      samplePayloadPrefix,
      sampleLength,
      sampleFrameNumber,
      sampleDetectedUtc,
      sampleAnalysisSource,
      createdAt: new Date().toISOString(),
    };
  }

  const signature = buildPacketSuppressionSignature(item);
  return {
    key: `packet:${signature}`,
    type: "packet",
    signature,
    label: getDetectionDisplayName(item),
    matcherLabel: route,
    sampleRoute: route,
    sampleInfo,
    samplePayloadPrefix,
    sampleLength,
    sampleFrameNumber,
    sampleDetectedUtc,
    sampleAnalysisSource,
    createdAt: new Date().toISOString(),
  };
}

function getSuppressionRuleForItem(item) {
  const opcode = normalizeOpcode(item?.opcode);
  if (opcode) {
    const opcodeRule = state.suppressionRules[`opcode:${opcode}`];
    if (opcodeRule) return opcodeRule;
  }

  const groupKey = String(item?.groupKey || "").trim();
  if (groupKey) {
    const groupRule = state.suppressionRules[`group:${groupKey}`];
    if (groupRule) return groupRule;
  }

  const packetKey = `packet:${buildPacketSuppressionSignature(item)}`;
  return state.suppressionRules[packetKey] || null;
}

function isSuppressedItem(item) {
  return Boolean(getSuppressionRuleForItem(item));
}

function getSuppressionRules() {
  if (runtimeCache.suppressionRulesVersion === runtimeCache.suppressionVersion) {
    return runtimeCache.suppressionRules;
  }
  runtimeCache.suppressionRules = Object.values(state.suppressionRules || {}).sort((left, right) => {
    const rightTime = Date.parse(right.createdAt || "") || 0;
    const leftTime = Date.parse(left.createdAt || "") || 0;
    return rightTime - leftTime;
  });
  runtimeCache.suppressionRulesVersion = runtimeCache.suppressionVersion;
  return runtimeCache.suppressionRules;
}

function getEntriesForOpcode(opcode) {
  const normalized = normalizeOpcode(opcode);
  if (!normalized) return [];
  getAllEntries();
  return runtimeCache.entriesByOpcode.get(normalized) || [];
}

function getOpcodeKnowledgeMeta(detection) {
  const opcodes = [...getDetectionOpcodes(detection)];
  if (!opcodes.length) {
    return {
      knownOpcode: false,
      unknownOpcode: false,
      onlyKnownOpcode: false,
    };
  }

  let knownOpcode = false;
  let unknownOpcode = false;
  for (const opcode of opcodes) {
    if (getEntriesForOpcode(opcode).length) {
      knownOpcode = true;
    } else {
      unknownOpcode = true;
    }
  }

  return {
    knownOpcode,
    unknownOpcode,
    onlyKnownOpcode: knownOpcode && !unknownOpcode,
  };
}

function getDetectionPreference(detection) {
  const preferred = [];
  const direct = normalizeOpcode(detection?.opcode);
  if (direct) preferred.push(direct);
  for (const opcode of getDetectionOpcodes(detection)) {
    if (opcode && !preferred.includes(opcode)) preferred.push(opcode);
  }
  for (const opcode of preferred) {
    const preference = getOpcodePreference(opcode);
    if (preference.alias || preference.flagged) {
      return preference;
    }
  }
  const opcodePreference = getOpcodePreference(direct);
  if (opcodePreference.alias || opcodePreference.flagged) {
    return opcodePreference;
  }
  return getPacketPreference(detection);
}

function getDefaultDetectionLabel(detection) {
  if (detection?.matchedEntries?.length) {
    return detection.matchedEntries.map((entry) => entry.rof2_name || entry.eqemu_name || entry.rof2_opcode).join(", ");
  }
  if (detection?.names?.length) {
    return detection.names.join(", ");
  }
  if (detection?.ignored) {
    return "Suppressed UDP noise";
  }
  if (detection?.opcode) {
    return `Candidate ${detection.opcode}`;
  }
  return "UDP traffic";
}

function getDetectionDisplayName(detection) {
  const preference = getDetectionPreference(detection);
  return preference.alias || getDefaultDetectionLabel(detection);
}

function loadPersistedState() {
  try {
    const saved = JSON.parse(localStorage.getItem(STORAGE_KEY) || "{}");
    state.entryState = Object.fromEntries(
      Object.entries(saved.entryState || {}).map(([id, entry]) => [
        id,
        {
          ...entry,
          status: entry?.status === "patch-known" ? "validated" : entry?.status,
        },
      ]),
    );
    state.bookmarkState = saved.bookmarkState || {};
    state.customEntries = saved.customEntries || [];
    state.opcodePreferences = saved.opcodePreferences || {};
    state.packetPreferences = saved.packetPreferences || {};
    state.suppressionRules = saved.suppressionRules || {};
  } catch {
    state.entryState = {};
    state.bookmarkState = {};
    state.customEntries = [];
    state.opcodePreferences = {};
    state.packetPreferences = {};
    state.suppressionRules = {};
  }
  invalidateEntryCaches();
  invalidatePreferenceCaches();
  invalidateSuppressionCaches();
}

function persistState() {
  localStorage.setItem(
    STORAGE_KEY,
    JSON.stringify({
      version: 1,
      updatedAt: new Date().toISOString(),
      entryState: state.entryState,
      bookmarkState: state.bookmarkState,
      customEntries: state.customEntries,
      opcodePreferences: state.opcodePreferences,
      packetPreferences: state.packetPreferences,
      suppressionRules: state.suppressionRules,
    }),
  );
}

function setCustomEntryDrawer(open, options = {}) {
  const overlay = Boolean(options.overlay);
  state.customEntryOverlayMode = open ? overlay : false;
  els.customEntryDrawer.hidden = !open;
  els.customEntryDrawer.classList.toggle("custom-entry-overlay", open && state.customEntryOverlayMode);
  els.toggleCustomEntryButton.setAttribute("aria-expanded", String(open));
}

function setSettingsDrawer(open) {
  els.settingsDrawer.hidden = !open;
  els.toggleSettingsButton.setAttribute("aria-expanded", String(open));
}

function syncModalOpenClass() {
  const anyOpen = state.inspectorOpen || state.workflowModalOpen || state.bookmarkModalOpen || state.liveMonitorOpen || state.renameOpcodeModalOpen || !els.detectionAlertModal.hidden;
  document.body.classList.toggle("modal-open", anyOpen);
}

function setInspectorModal(open) {
  const shouldOpen = Boolean(open && state.selectedId);
  state.inspectorOpen = shouldOpen;
  els.inspectorModal.hidden = !shouldOpen;
  syncModalOpenClass();
}

function setWorkflowModal(open) {
  const shouldOpen = Boolean(open && state.selectedId);
  state.workflowModalOpen = shouldOpen;
  els.workflowModal.hidden = !shouldOpen;
  if (shouldOpen) renderWorkflowModal();
  syncModalOpenClass();
}

function setDetectionAlertModal(open) {
  const shouldOpen = Boolean(open && state.currentAlertMatches.length);
  els.detectionAlertModal.hidden = !shouldOpen;
  syncModalOpenClass();
}

function setBookmarkModal(open) {
  const shouldOpen = Boolean(open && state.selectedBookmarkId);
  state.bookmarkModalOpen = shouldOpen;
  els.bookmarkModal.hidden = !shouldOpen;
  syncModalOpenClass();
}

function setLiveMonitorModal(open) {
  const shouldOpen = Boolean(open);
  state.liveMonitorOpen = shouldOpen;
  els.liveMonitorModal.hidden = !shouldOpen;
  if (!shouldOpen) {
    closeLiveMonitorContextMenu();
  }
  syncModalOpenClass();
}

function setRenameOpcodeModal(open) {
  const shouldOpen = Boolean(open && (state.renameOpcodeTarget || state.renamePacketTarget));
  state.renameOpcodeModalOpen = shouldOpen;
  els.renameOpcodeModal.hidden = !shouldOpen;
  if (!shouldOpen) {
    state.renameOpcodeTarget = "";
    state.renamePacketTarget = "";
  }
  syncModalOpenClass();
}

function getStatusLabel(status) {
  return STATUS_LABELS[status] || status;
}

function getSourceLabel(source) {
  return SOURCE_LABELS[source] || source;
}

function getAlignmentLabel(alignment) {
  return ALIGNMENT_LABELS[alignment] || alignment;
}

function getDefaultSortDirection(sort) {
  return SORT_DEFAULT_DIRECTIONS[sort] || "asc";
}

function getEntryDisplayName(entry) {
  return entry.rof2_name || entry.eqemu_name || "";
}

function getEntryNoteText(entry) {
  const parts = [];
  if (entry.packet_signature) parts.push(`Packet: ${entry.packet_signature}`);
  if (entry.packet_family_key) parts.push(`Family: ${entry.packet_family_key}`);
  if (entry.userNotes) parts.push(entry.userNotes);
  else if (entry.notes) parts.push(entry.notes);
  if (entry.extra && !parts.includes(entry.extra)) parts.push(entry.extra);
  return parts.join(" • ");
}

function compareText(left, right) {
  return String(left || "\uffff").localeCompare(String(right || "\uffff"), undefined, {
    numeric: true,
    sensitivity: "base",
  });
}

function compareBoolean(left, right) {
  return Number(Boolean(left)) - Number(Boolean(right));
}

function compareOpcode(left, right) {
  return compareText(left.rof2_opcode, right.rof2_opcode);
}

function applyDirection(compareValue, direction = state.filters.sortDirection) {
  return direction === "desc" ? -compareValue : compareValue;
}

function syncSortUi() {
  els.sortFilter.value = state.filters.sort;
  for (const button of els.sortButtons) {
    const active = button.dataset.sort === state.filters.sort;
    const indicator = button.querySelector(".sort-indicator");
    const th = button.closest("th");
    button.classList.toggle("active", active);
    button.dataset.direction = active ? state.filters.sortDirection : "";
    button.setAttribute("aria-pressed", String(active));
    if (indicator) {
      indicator.textContent = active ? (state.filters.sortDirection === "desc" ? "↓" : "↑") : "↕";
    }
    if (th) {
      th.setAttribute("aria-sort", active ? (state.filters.sortDirection === "desc" ? "descending" : "ascending") : "none");
    }
  }
}

function setSort(sort, options = {}) {
  const { toggle = false } = options;
  if (toggle && state.filters.sort === sort) {
    state.filters.sortDirection = state.filters.sortDirection === "desc" ? "asc" : "desc";
  } else {
    const changed = state.filters.sort !== sort;
    state.filters.sort = sort;
    if (changed || !state.filters.sortDirection) {
      state.filters.sortDirection = getDefaultSortDirection(sort);
    }
  }
  syncSortUi();
}

function renderStatusOptions() {
  const options = STATUS_OPTIONS
    .map((status) => `<option value="${status}">${getStatusLabel(status)}</option>`)
    .join("");
  els.entryStatus.innerHTML = options;
  els.statusFilter.insertAdjacentHTML("beforeend", options);
}

function statusRank(status) {
  return {
    validated: 0,
    suspected: 1,
    watching: 2,
    "needs-capture": 3,
    ignored: 4,
    "sheet-known": 5,
    "patch-known": 6,
    unreviewed: 7,
  }[status] ?? 99;
}

function formatTimestamp(value) {
  if (!value) return "Never touched locally";
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return "Never touched locally";
  return `Touched ${date.toLocaleString([], { dateStyle: "medium", timeStyle: "short" })}`;
}

function formatCompactTimestamp(value) {
  if (!value) return "Unmarked time";
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return "Unmarked time";
  return date.toLocaleString([], { dateStyle: "medium", timeStyle: "short" });
}

function escapeHtml(value) {
  return String(value ?? "")
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#39;");
}

function normalizeMonitorText(value) {
  return String(value ?? "").replaceAll("â†’", "->");
}

function setSessionActionMessage(message, tone = "muted") {
  els.sessionActionMessage.textContent = message;
  els.sessionActionMessage.dataset.tone = tone;
}

function setRegistryActionMessage(message, tone = "muted") {
  els.registryActionMessage.textContent = message;
  els.registryActionMessage.dataset.tone = tone;
}

function getActivityById(id) {
  getActivityEntries();
  return runtimeCache.activityById.get(id) || null;
}

function closeLiveMonitorContextMenu() {
  state.liveMonitorContextMenu = { open: false, detectionId: "", opcode: "", groupKey: "", x: 0, y: 0 };
  els.liveMonitorContextMenu.hidden = true;
}

function getLinkedRegistryEntry(detection) {
  if (!detection) return null;
  for (const matched of detection.matchedEntries || []) {
    if (matched?.id) {
      const byId = getEntryById(matched.id);
      if (byId) return byId;
    }
    const matchedOpcode = matched?.rof2_opcode || matched?.eqemu_opcode || matched?.opcode || "";
    const opcodeEntry = getEntriesForOpcode(matchedOpcode)[0];
    if (opcodeEntry) return opcodeEntry;
  }
  const directEntry = getEntriesForOpcode(detection.opcode)[0];
  if (directEntry) return directEntry;
  for (const candidate of detection.candidates || []) {
    const candidateEntry = getEntriesForOpcode(candidate?.opcode)[0];
    if (candidateEntry) return candidateEntry;
  }
  return null;
}

function openLiveMonitorContextMenu(detection, x, y, options = {}) {
  const opcode = normalizeOpcode(detection?.opcode);
  const groupKey = String(options.groupKey || "").trim();
  const packetPreference = getPacketPreference(detection, { groupKey });
  state.liveMonitorContextMenu = {
    open: true,
    detectionId: detection.id,
    opcode,
    groupKey,
    x,
    y,
  };

  const displayName = getOpcodePreference(opcode).alias || packetPreference.alias || getDefaultDetectionLabel(detection);
  const preference = getOpcodePreference(opcode);
  const hasEntry = Boolean(getLinkedRegistryEntry(detection));
  const suppressionRule = buildSuppressionRule(detection, { groupKey });
  const alreadySuppressed = Boolean(state.suppressionRules[suppressionRule.key]);
  els.liveMonitorContextTitle.textContent = opcode
    ? `${opcode}${displayName && displayName !== opcode ? ` • ${displayName}` : ""}`
    : displayName;
  els.toggleLiveMonitorFlagButton.textContent = preference.flagged ? "Remove opcode alert flag" : "Flag opcode for alerts";
  els.renameLiveMonitorOpcodeButton.textContent = opcode ? "Rename opcode label" : "Rename packet nickname";
  els.renameLiveMonitorOpcodeButton.hidden = !opcode && !packetPreference.key;
  els.toggleLiveMonitorFlagButton.hidden = !opcode;
  els.suppressLiveMonitorItemButton.textContent = alreadySuppressed
    ? "Already suppressed"
    : suppressionRule.type === "opcode"
      ? "Suppress opcode from live feed"
      : suppressionRule.type === "group"
        ? "Suppress feed group from live feed"
      : "Suppress packet pattern from live feed";
  els.suppressLiveMonitorItemButton.disabled = alreadySuppressed;
  els.openLiveMonitorEntryButton.disabled = !hasEntry;
  els.liveMonitorContextMenu.hidden = false;

  requestAnimationFrame(() => {
    const menuRect = els.liveMonitorContextMenu.getBoundingClientRect();
    const left = Math.max(16, Math.min(x, window.innerWidth - menuRect.width - 16));
    const top = Math.max(16, Math.min(y, window.innerHeight - menuRect.height - 16));
    els.liveMonitorContextMenu.style.left = `${left}px`;
    els.liveMonitorContextMenu.style.top = `${top}px`;
  });
}

function updateOpcodePreference(opcode, patch) {
  const normalized = normalizeOpcode(opcode);
  if (!normalized) return;

  const next = {
    ...(state.opcodePreferences[normalized] || {}),
    ...patch,
  };
  if (!next.alias && !next.flagged) {
    delete state.opcodePreferences[normalized];
  } else {
    state.opcodePreferences[normalized] = next;
  }

  const matchingEntries = getEntriesForOpcode(normalized);
  if (matchingEntries.length && Object.prototype.hasOwnProperty.call(patch, "flagged")) {
    for (const entry of matchingEntries) {
      state.entryState[entry.id] = {
        ...(state.entryState[entry.id] || {}),
        alertEnabled: Boolean(patch.flagged),
        updatedAt: new Date().toISOString(),
      };
    }
  }

  invalidateEntryCaches();
  invalidatePreferenceCaches();
  persistState();
  renderStats();
  renderTable();
  renderLiveSession();
  renderBookmarks();
  renderBookmarkModal();
  renderInspector();
  renderDetectionAlertModal();
}

function updatePacketPreference(key, patch) {
  if (!key) return;
  const next = {
    ...(state.packetPreferences[key] || {}),
    ...patch,
  };
  if (!next.alias) {
    delete state.packetPreferences[key];
  } else {
    state.packetPreferences[key] = next;
  }

  invalidatePreferenceCaches();
  persistState();
  renderLiveSession();
  renderLiveMonitor();
  renderDetectionAlertModal();
}

function updateSuppressionRule(rule) {
  if (!rule?.key) return;
  if (state.suppressionRules[rule.key]) return;
  state.suppressionRules[rule.key] = {
    ...rule,
    createdAt: rule.createdAt || new Date().toISOString(),
  };
  invalidateSuppressionCaches();
  persistState();
  renderLiveSession();
  renderLiveMonitor();
  renderDetectionAlertModal();
}

function removeSuppressionRule(ruleKey) {
  if (!ruleKey || !state.suppressionRules[ruleKey]) return;
  delete state.suppressionRules[ruleKey];
  invalidateSuppressionCaches();
  persistState();
  renderLiveSession();
  renderLiveMonitor();
  renderDetectionAlertModal();
}

function openRenameOpcodeModal(opcode) {
  const normalized = normalizeOpcode(opcode);
  if (!normalized) return;
  state.renameOpcodeTarget = normalized;
  state.renamePacketTarget = "";
  const preference = getOpcodePreference(normalized);
  const detection = getActivityById(state.liveMonitorContextMenu.detectionId);
  const currentLabel = detection ? getDetectionDisplayName(detection) : normalized;
  els.renameOpcodeTitle.textContent = `Rename ${normalized}`;
  els.renameOpcodeSummary.textContent = `Future live detections for ${normalized} will use this label instead of ${currentLabel}.`;
  els.renameOpcodeInput.value = preference.alias || "";
  setRenameOpcodeModal(true);
  closeLiveMonitorContextMenu();
  requestAnimationFrame(() => els.renameOpcodeInput.focus());
}

function openRenamePacketModal(detection, options = {}) {
  const preference = getPacketPreference(detection, options);
  if (!preference.key) return;
  state.renameOpcodeTarget = "";
  state.renamePacketTarget = preference.key;
  const currentLabel = preference.alias || getDetectionDisplayName(detection);
  els.renameOpcodeTitle.textContent = "Rename packet nickname";
  els.renameOpcodeSummary.textContent = `Future live feed matches for this packet record will use this nickname instead of ${currentLabel}.`;
  els.renameOpcodeInput.value = preference.alias || "";
  setRenameOpcodeModal(true);
  closeLiveMonitorContextMenu();
  requestAnimationFrame(() => els.renameOpcodeInput.focus());
}

function seedCustomEntryFromDetection(detection, options = {}) {
  if (!detection) return;
  const groupKey = normalizePacketFamilyKey(options.groupKey || detection.groupKey);
  const label = getDetectionDisplayName(detection);
  const confidence = detection.eqConfidence && detection.eqConfidence !== "none" ? detection.eqConfidence : "medium";
  const details = [
    detection.payloadPrefix ? `Packet Signature: ${detection.payloadPrefix}` : "",
    groupKey ? `Packet Family: ${groupKey}` : "",
    `${detection.src || "?"}:${detection.srcport || "?"} -> ${detection.dst || "?"}:${detection.dstport || "?"}`,
    detection.info || "",
  ].filter(Boolean);
  els.customOpcode.value = normalizeOpcode(detection.opcode);
  els.customName.value = label && label !== "UDP traffic" ? label : "";
  els.customPacketSignature.value = detection.payloadPrefix || "";
  els.customPacketFamilyKey.value = groupKey;
  els.customConfidence.value = confidence;
  els.customNotes.value = details.join("\n");
  state.ignoreCustomEntryOutsideClickUntil = Date.now() + 250;
  closeLiveMonitorContextMenu();
  requestAnimationFrame(() => {
    setSettingsDrawer(false);
    setCustomEntryDrawer(true, { overlay: state.liveMonitorOpen });
    els.customName.focus();
  });
}

function saveOpcodeAlias(value) {
  const alias = value.trim();
  const opcode = state.renameOpcodeTarget;
  if (opcode) {
    updateOpcodePreference(opcode, { alias });
    setRenameOpcodeModal(false);
    return;
  }
  const packetKey = state.renamePacketTarget;
  if (!packetKey) return;
  updatePacketPreference(packetKey, { alias });
  setRenameOpcodeModal(false);
}

function clearOpcodeAlias() {
  const opcode = state.renameOpcodeTarget;
  if (opcode) {
    updateOpcodePreference(opcode, { alias: "" });
    setRenameOpcodeModal(false);
    return;
  }
  const packetKey = state.renamePacketTarget;
  if (!packetKey) return;
  updatePacketPreference(packetKey, { alias: "" });
  setRenameOpcodeModal(false);
}

function openContextOpcodeEntry() {
  const detection = getActivityById(state.liveMonitorContextMenu.detectionId);
  const entry = getLinkedRegistryEntry(detection);
  if (!entry) return;
  state.selectedId = entry.id;
  closeLiveMonitorContextMenu();
  setLiveMonitorModal(false);
  setInspectorModal(true);
  renderTable();
  renderBookmarks();
  renderInspector();
}

function getSessionKey(live) {
  return `${live.sessionName || "session"}|${live.startedUtc || "start"}`;
}

function getDetectionOpcodes(detection) {
  const values = new Set();
  if (detection?.opcode) values.add(detection.opcode.toLowerCase());
  for (const candidate of detection?.candidates || []) {
    if (candidate?.opcode) values.add(String(candidate.opcode).toLowerCase());
  }
  return values;
}

function getFlaggedEntries() {
  getAllEntries();
  return [...runtimeCache.flaggedEntriesByOpcode.values()].flat();
}

function matchDetectionToEntries(detection) {
  const matches = [];
  const seen = new Set();
  getAllEntries();
  const addEntries = (entries) => {
    for (const entry of entries || []) {
      if (!entry?.id || seen.has(entry.id)) continue;
      seen.add(entry.id);
      matches.push(entry);
    }
  };

  const groupKey = normalizePacketFamilyKey(detection?.groupKey);
  if (groupKey) {
    addEntries(runtimeCache.flaggedEntriesByPacketFamilyKey.get(groupKey));
  }

  const opcodes = getDetectionOpcodes(detection);
  for (const opcode of opcodes) {
    addEntries(runtimeCache.flaggedEntriesByOpcode.get(normalizeOpcode(opcode)));
  }

  const payloadPrefix = normalizePacketSignature(detection?.payloadPrefix);
  if (payloadPrefix) {
    for (const [signature, entries] of runtimeCache.flaggedEntriesByPacketSignature.entries()) {
      if (!signature || !payloadPrefix.startsWith(signature)) continue;
      addEntries(entries);
    }
  }

  const customPacketAlias = getPacketPreference(detection).alias;
  const detectionLabelKey = normalizeLabelKey(customPacketAlias || getDetectionDisplayName(detection));
  if (detectionLabelKey.length >= 6) {
    for (const entry of runtimeCache.flaggedCustomEntries) {
      const entryLabelKey = normalizeLabelKey(getEntryDisplayName(entry) || entry.display_name || "");
      if (entryLabelKey.length < 6) continue;
      if (detectionLabelKey.includes(entryLabelKey) || entryLabelKey.includes(detectionLabelKey)) {
        addEntries([entry]);
      }
    }
  }
  return matches;
}

function hasFlaggedOpcodePreference(detection) {
  return getDetectionPreference(detection).flagged;
}

function getDetectionAlertLabels(detection) {
  const labels = [];
  const preference = getDetectionPreference(detection);
  if (preference.alias) {
    labels.push(preference.alias);
  }
  for (const entry of detection?.matchedEntries || []) {
    const label = entry.rof2_name || entry.eqemu_name || entry.rof2_opcode;
    if (label && !labels.includes(label)) labels.push(label);
  }
  for (const name of detection?.names || []) {
    if (name && !labels.includes(name)) labels.push(name);
  }
  if (!labels.length && detection?.opcode) {
    labels.push(detection.opcode);
  }
  return labels;
}

function markEntriesRecentlyDetected(entryIds) {
  for (const entryId of entryIds) {
    state.recentDetectedEntryIds.add(entryId);
    if (state.recentDetectionTimers[entryId]) {
      window.clearTimeout(state.recentDetectionTimers[entryId]);
    }
    state.recentDetectionTimers[entryId] = window.setTimeout(() => {
      state.recentDetectedEntryIds.delete(entryId);
      delete state.recentDetectionTimers[entryId];
      renderTable();
    }, 15000);
  }
}

function clearRecentActivityTimers() {
  for (const timerId of Object.values(state.recentActivityTimers)) {
    window.clearTimeout(timerId);
  }
  state.recentActivityTimers = {};
  state.recentActivityIds = new Set();
}

function clearRecentCountTimers() {
  for (const timerId of Object.values(state.recentCountTimers)) {
    window.clearTimeout(timerId);
  }
  state.recentCountTimers = {};
  state.recentCountEntryIds = new Set();
}

function markActivityItemsRecent(activityIds) {
  for (const activityId of activityIds) {
    if (!activityId) continue;
    state.recentActivityIds.add(activityId);
    if (state.recentActivityTimers[activityId]) {
      window.clearTimeout(state.recentActivityTimers[activityId]);
    }
    state.recentActivityTimers[activityId] = window.setTimeout(() => {
      state.recentActivityIds.delete(activityId);
      delete state.recentActivityTimers[activityId];
      if (state.liveMonitorOpen) {
        renderLiveMonitor();
      }
    }, 1800);
  }
}

function markEntriesCountRecentlyChanged(entryIds) {
  let changed = false;
  for (const entryId of entryIds) {
    if (!entryId) continue;
    changed = true;
    state.recentCountEntryIds.add(entryId);
    if (state.recentCountTimers[entryId]) {
      window.clearTimeout(state.recentCountTimers[entryId]);
    }
    state.recentCountTimers[entryId] = window.setTimeout(() => {
      state.recentCountEntryIds.delete(entryId);
      delete state.recentCountTimers[entryId];
      renderTable();
    }, 1800);
  }
  if (changed) {
    renderTable();
  }
}

async function primeAudioContext() {
  if (!window.AudioContext && !window.webkitAudioContext) return;
  if (!state.audioContext) {
    const AudioCtor = window.AudioContext || window.webkitAudioContext;
    state.audioContext = new AudioCtor();
  }
  if (state.audioContext.state === "suspended") {
    try {
      await state.audioContext.resume();
    } catch {
      return;
    }
  }
}

async function playAlertTone() {
  await primeAudioContext();
  if (!state.audioContext || state.audioContext.state !== "running") return;

  const now = state.audioContext.currentTime;
  const sequence = [
    { start: 0, duration: 0.12, frequency: 740 },
    { start: 0.16, duration: 0.14, frequency: 880 },
  ];

  for (const tone of sequence) {
    const oscillator = state.audioContext.createOscillator();
    const gain = state.audioContext.createGain();
    oscillator.type = "triangle";
    oscillator.frequency.value = tone.frequency;
    gain.gain.setValueAtTime(0.0001, now + tone.start);
    gain.gain.exponentialRampToValueAtTime(0.1, now + tone.start + 0.02);
    gain.gain.exponentialRampToValueAtTime(0.0001, now + tone.start + tone.duration);
    oscillator.connect(gain);
    gain.connect(state.audioContext.destination);
    oscillator.start(now + tone.start);
    oscillator.stop(now + tone.start + tone.duration + 0.02);
  }
}

async function copyTextToClipboard(text) {
  const value = String(text || "");
  if (!value) return false;
  try {
    await navigator.clipboard.writeText(value);
    return true;
  } catch {
    const area = document.createElement("textarea");
    area.value = value;
    area.setAttribute("readonly", "");
    area.style.position = "fixed";
    area.style.opacity = "0";
    document.body.appendChild(area);
    area.select();
    try {
      document.execCommand("copy");
      return true;
    } catch {
      return false;
    } finally {
      document.body.removeChild(area);
    }
  }
}

function decorateEntry(entry) {
  const saved = state.entryState[entry.id] || {};
  const derivedStatus = entry.eqemu_name
    ? "validated"
    : entry.rof2_name
      ? "sheet-known"
      : "unreviewed";
  const derivedWorkflowStage = normalizeWorkflowStage(
    saved.workflowStage
    || (saved.status === "validated" || derivedStatus === "validated" ? "confirmed" : "")
    || (normalizeOpcode(entry.rof2_opcode) ? "opcode-hypothesis" : "candidate"),
  );

  return {
    ...entry,
    normalizedOpcode: normalizeOpcode(entry.rof2_opcode),
    normalizedPacketSignature: normalizePacketSignature(entry.packet_signature),
    normalizedPacketFamilyKey: normalizePacketFamilyKey(entry.packet_family_key),
    workflowStage: derivedWorkflowStage,
    workflowRepeatable: Boolean(saved.workflowRepeatable),
    workflowIsolated: Boolean(saved.workflowIsolated),
    workflowHypothesis: Boolean(saved.workflowHypothesis),
    workflowConfirmed: Boolean(saved.workflowConfirmed || derivedWorkflowStage === "confirmed"),
    status: saved.status || derivedStatus,
    confidence: saved.confidence || "unknown",
    alertEnabled: Boolean(saved.alertEnabled),
    tags: saved.tags || "",
    userNotes: saved.userNotes || "",
    updatedAt: saved.updatedAt || "",
    tracked: Boolean(
      saved.userNotes
      || saved.tags
      || saved.updatedAt
      || !["unreviewed", "sheet-known", "validated", "patch-known"].includes(saved.status || derivedStatus),
    ),
  };
}

function buildRegistryEntryFingerprints(entry) {
  if (!entry || entry.source_type === "custom") return [];
  const source = String(entry.source_type || "");
  const opcode = normalizeOpcode(entry.rof2_opcode);
  const rof2Name = normalizeLabelKey(entry.rof2_name);
  const eqemuName = normalizeLabelKey(entry.eqemu_name);
  const signature = normalizePacketSignature(entry.packet_signature);
  const familyKey = normalizePacketFamilyKey(entry.packet_family_key);
  const combinedNames = [rof2Name, eqemuName].filter(Boolean).join("|");
  const fingerprints = [];
  const pushFingerprint = (value) => {
    if (value && !fingerprints.includes(value)) fingerprints.push(value);
  };

  if (opcode && combinedNames) pushFingerprint(`${source}|opcode+names|${opcode}|${combinedNames}`);
  if (opcode) pushFingerprint(`${source}|opcode|${opcode}`);
  if (signature && familyKey) pushFingerprint(`${source}|signature+family|${signature}|${familyKey}`);
  if (signature) pushFingerprint(`${source}|signature|${signature}`);
  if (familyKey) pushFingerprint(`${source}|family|${familyKey}`);
  if (combinedNames) pushFingerprint(`${source}|names|${combinedNames}`);
  if (rof2Name) pushFingerprint(`${source}|rof2|${rof2Name}`);
  if (eqemuName) pushFingerprint(`${source}|eqemu|${eqemuName}`);
  pushFingerprint(`${source}|id|${entry.id || ""}`);
  return fingerprints;
}

function buildRescanIdMap(previousEntries, nextEntries) {
  const buckets = new Map();
  for (const entry of nextEntries || []) {
    for (const fingerprint of buildRegistryEntryFingerprints(entry)) {
      if (!buckets.has(fingerprint)) buckets.set(fingerprint, []);
      buckets.get(fingerprint).push(entry.id);
    }
  }

  const usedIds = new Set();
  const idMap = new Map();
  for (const entry of previousEntries || []) {
    if (!entry?.id || entry.source_type === "custom") continue;
    for (const fingerprint of buildRegistryEntryFingerprints(entry)) {
      const candidates = buckets.get(fingerprint) || [];
      const nextId = candidates.find((candidateId) => !usedIds.has(candidateId));
      if (!nextId) continue;
      idMap.set(entry.id, nextId);
      usedIds.add(nextId);
      break;
    }
  }
  return idMap;
}

function remapBookmarkStateIds(bookmarkState, idMap) {
  const nextBookmarkState = {};
  for (const [bookmarkId, bookmark] of Object.entries(bookmarkState || {})) {
    nextBookmarkState[bookmarkId] = {
      ...bookmark,
      linkedEntryId: bookmark?.linkedEntryId ? (idMap.get(bookmark.linkedEntryId) || bookmark.linkedEntryId) : "",
    };
  }
  return nextBookmarkState;
}

function remapRegistryStateForRescan(nextRegistry) {
  const previousEntries = Array.isArray(state.data?.entries) ? state.data.entries : [];
  const nextEntries = Array.isArray(nextRegistry?.entries) ? nextRegistry.entries : [];
  const idMap = buildRescanIdMap(previousEntries, nextEntries);
  const nextEntryState = {};

  for (const [entryId, savedState] of Object.entries(state.entryState || {})) {
    const nextId = idMap.get(entryId) || entryId;
    nextEntryState[nextId] = {
      ...(nextEntryState[nextId] || {}),
      ...savedState,
    };
  }

  state.entryState = nextEntryState;
  state.bookmarkState = remapBookmarkStateIds(state.bookmarkState, idMap);
  state.selectedId = state.selectedId ? (idMap.get(state.selectedId) || state.selectedId) : null;
}

function getAllEntries() {
  if (runtimeCache.allEntriesVersion === runtimeCache.entryVersion) {
    return runtimeCache.allEntries;
  }
  const base = (state.data?.entries || []).map(decorateEntry);
  const custom = state.customEntries.map((entry) => decorateEntry({ ...entry, source_type: "custom", alignment: "custom" }));
  const allEntries = [...base, ...custom];
  const entryById = new Map();
  const entriesByOpcode = new Map();
  const entriesByPacketSignature = new Map();
  const entriesByPacketFamilyKey = new Map();
  const flaggedEntriesByOpcode = new Map();
  const flaggedEntriesByPacketSignature = new Map();
  const flaggedEntriesByPacketFamilyKey = new Map();
  const flaggedCustomEntries = [];
  for (const entry of allEntries) {
    entryById.set(entry.id, entry);
    const opcode = entry.normalizedOpcode;
    if (opcode) {
      if (!entriesByOpcode.has(opcode)) entriesByOpcode.set(opcode, []);
      entriesByOpcode.get(opcode).push(entry);
      if (entry.alertEnabled) {
        if (!flaggedEntriesByOpcode.has(opcode)) flaggedEntriesByOpcode.set(opcode, []);
        flaggedEntriesByOpcode.get(opcode).push(entry);
      }
    }
    const packetSignature = entry.normalizedPacketSignature;
    if (packetSignature) {
      if (!entriesByPacketSignature.has(packetSignature)) entriesByPacketSignature.set(packetSignature, []);
      entriesByPacketSignature.get(packetSignature).push(entry);
      if (entry.alertEnabled) {
        if (!flaggedEntriesByPacketSignature.has(packetSignature)) flaggedEntriesByPacketSignature.set(packetSignature, []);
        flaggedEntriesByPacketSignature.get(packetSignature).push(entry);
      }
    }
    const packetFamilyKey = entry.normalizedPacketFamilyKey;
    if (packetFamilyKey) {
      if (!entriesByPacketFamilyKey.has(packetFamilyKey)) entriesByPacketFamilyKey.set(packetFamilyKey, []);
      entriesByPacketFamilyKey.get(packetFamilyKey).push(entry);
      if (entry.alertEnabled) {
        if (!flaggedEntriesByPacketFamilyKey.has(packetFamilyKey)) flaggedEntriesByPacketFamilyKey.set(packetFamilyKey, []);
        flaggedEntriesByPacketFamilyKey.get(packetFamilyKey).push(entry);
      }
    }
    if (entry.alertEnabled && entry.source_type === "custom" && !opcode && !packetSignature && !packetFamilyKey) {
      flaggedCustomEntries.push(entry);
    }
  }
  runtimeCache.allEntries = allEntries;
  runtimeCache.entryById = entryById;
  runtimeCache.entriesByOpcode = entriesByOpcode;
  runtimeCache.entriesByPacketSignature = entriesByPacketSignature;
  runtimeCache.entriesByPacketFamilyKey = entriesByPacketFamilyKey;
  runtimeCache.flaggedEntriesByOpcode = flaggedEntriesByOpcode;
  runtimeCache.flaggedEntriesByPacketSignature = flaggedEntriesByPacketSignature;
  runtimeCache.flaggedEntriesByPacketFamilyKey = flaggedEntriesByPacketFamilyKey;
  runtimeCache.flaggedCustomEntries = flaggedCustomEntries;
  runtimeCache.allEntriesVersion = runtimeCache.entryVersion;
  return allEntries;
}

function getEntryById(id) {
  getAllEntries();
  return runtimeCache.entryById.get(id) || null;
}

function getLinkedMarkerCount(entryId) {
  if (!entryId) return 0;
  return getBookmarks().filter((bookmark) => bookmark.linkedEntryId === entryId).length;
}

function getWorkflowIdentityLabel(entry) {
  if (entry.normalizedOpcode) return "Opcode candidate recorded";
  if (entry.normalizedPacketFamilyKey) return "Packet family locked";
  if (entry.normalizedPacketSignature) return "Packet signature only";
  return "Name-only candidate";
}

function getWorkflowChecklist(entry) {
  const stageIndex = getWorkflowStageIndex(entry.workflowStage);
  return {
    repeatable: entry.workflowRepeatable || stageIndex >= 1,
    isolated: entry.workflowIsolated || stageIndex >= 2,
    hypothesis: entry.workflowHypothesis || stageIndex >= 3 || Boolean(entry.normalizedOpcode),
    confirmed: entry.workflowConfirmed || stageIndex >= 4 || entry.status === "validated",
  };
}

function getWorkflowNextStep(entry, evidence, analysis = {}) {
  switch (normalizeWorkflowStage(entry.workflowStage)) {
    case "candidate":
      if (!entry.alertEnabled) return "Arm the live alert so OCC can surface this packet family immediately when it appears again.";
      if (evidence.sessionCount < 3) return "Repeat the exact UI action in short runs until this row increments several times in the current capture.";
      return "Add one or more interaction markers for this action and confirm the same row increments tightly around those markers.";
    case "repeatable":
      if (!evidence.markerCount) return "Link at least one interaction marker to this row so the timing evidence stays attached to the candidate.";
      return "Run a clean capture where only the target action is performed, then compare the neighboring packets around each linked marker.";
    case "isolated":
      if (!entry.normalizedPacketFamilyKey && !entry.normalizedPacketSignature) return "Seed a packet family or representative packet signature from the live feed so this candidate has a stable identity.";
      if (analysis.bestCandidate) return `Automated analysis found ${analysis.bestCandidate.opcode}${analysis.bestCandidate.offset !== undefined ? ` at byte ${analysis.bestCandidate.offset}` : ""}. Record it as a test opcode, then validate it across repeated runs and nearby packets.`;
      if (analysis.payloadAnalysis?.sampleCount >= 2) return "OCC compared the repeated payloads but did not find a stable 16-bit window yet. Capture a few more isolated triggers or compare the neighboring packets around a marker to find the inner request packet.";
      return "Run the automated analysis again after more isolated captures so OCC can compare repeated payload bytes, request/response timing, and any stable inner 16-bit values.";
    case "opcode-hypothesis":
      if (analysis.bestCandidate && !entry.test_opcode) return `Record ${analysis.bestCandidate.opcode} as the working hypothesis, then compare it against EQEmu handlers, nearby packets, and repeat runs.`;
      if (!entry.normalizedOpcode) return "Record the best opcode hypothesis in the Opcode field once you can point to a stable inner value.";
      return "Validate the opcode hypothesis across repeated runs and compare it against EQEmu handlers, patch data, or other references.";
    case "confirmed":
    default:
      if (entry.status !== "validated") return "The mapping looks complete. Promote Status to Confirmed when the evidence is strong enough, then keep notes and markers as support.";
      return "Workflow complete. Preserve notes, packet family, and linked markers so the mapping stays easy to audit later.";
  }
}

function buildWorkflowReferenceHints(entry, analysis = {}) {
  const hints = [];
  const candidateOpcode = analysis.bestCandidate?.opcode || "";
  const candidateOffset = analysis.bestCandidate?.offset;
  const bestSource = analysis.bestSource?.[0] || "";

  if (!candidateOpcode) {
    hints.push("1. Decode first: treat the repeated packet family as evidence, not a confirmed opcode. If the decode source is raw/unknown, assume the outer capture may still be combined, oversized, compressed, or otherwise wrapped.");
  } else if (analysis.bestCandidate?.source === "raw-window") {
    hints.push(`1. Candidate opcode: OCC found ${candidateOpcode} from a stable raw 16-bit window${candidateOffset !== undefined ? ` at byte ${candidateOffset}` : ""}. Treat it as a hypothesis until it survives code/reference checks.`);
  } else {
    hints.push(`1. Candidate opcode: OCC found ${candidateOpcode}${candidateOffset !== undefined ? ` at offset ${candidateOffset}` : ""} from decoded packet analysis. This is strong enough to move into reference and code validation.`);
  }

  hints.push("2. ShowEQ cross-check: compare the suspected opcode against the nearest ShowEQ patch folder, using patch date and the timestamps inside worldopcodes.xml / zoneopcodes.xml. ShowEQ only maps the opcodes it needs, so absence there is not disproof.");

  if (!candidateOpcode) {
    hints.push("3. Packet structure pass: compare repeated payloads, then inspect neighboring packets around markers. Look for string lengths, null-terminated strings, repeated routes, and request/response pairs before naming an opcode.");
  } else {
    hints.push(`3. Client opcode path: if ShowEQ and EQEmu still do not confirm ${candidateOpcode}, compare opcode dispatch blocks in the current client and the emulator-supported client. Look for cmp/jump patterns around the opcode and follow the executed code path.`);
  }

  if (!bestSource || bestSource === "raw/unknown") {
    hints.push("4. Code-assisted decode: the EQEmu guide is explicit that packet capture alone is not enough for many packets. If this family stays opaque, the next reliable step is code-based unpacking / decompression before trying to finalize the opcode.");
  }

  hints.push("5. Emulator-side proof: once you have a probable opcode or field layout, use a Lua quest packet sender to replay bytes back to the client and adjust fields one byte at a time. A crash is still useful evidence because it narrows the packet structure.");

  if (entry.normalizedPacketFamilyKey && !candidateOpcode) {
    hints.push("6. Current row state: this entry is packet-family locked. Keep that family as the identity anchor; do not overwrite it with a single packet signature unless the payload prefix is truly stable.");
  }

  return hints.join("\n\n");
}

function getWorkflowStagePatch(stage) {
  const normalizedStage = normalizeWorkflowStage(stage);
  const index = getWorkflowStageIndex(normalizedStage);
  return {
    workflowStage: normalizedStage,
    workflowRepeatable: index >= 1,
    workflowIsolated: index >= 2,
    workflowHypothesis: index >= 3,
    workflowConfirmed: index >= 4,
  };
}

function getWorkflowBytePairs(value) {
  const normalized = normalizePacketSignature(value);
  if (!normalized || normalized.length < 2) return [];
  const pairs = [];
  for (let index = 0; index < normalized.length - 1; index += 2) {
    pairs.push(normalized.slice(index, index + 2));
  }
  return pairs;
}

function formatWorkflowOffsets(offsets) {
  if (!offsets?.length) return "none";
  return offsets.join(", ");
}

function analyzeWorkflowPayloads(matches) {
  const byteSamples = matches
    .map((item) => getWorkflowBytePairs(item.payloadPrefix))
    .filter((pairs) => pairs.length >= 2);

  if (byteSamples.length < 2) {
    return {
      sampleCount: byteSamples.length,
      comparableByteCount: 0,
      stableOffsets: [],
      variableOffsets: [],
      stableMask: "",
      windows: [],
      bestWindow: null,
    };
  }

  const comparableByteCount = Math.min(
    24,
    ...byteSamples.map((pairs) => pairs.length),
  );
  if (comparableByteCount < 2) {
    return {
      sampleCount: byteSamples.length,
      comparableByteCount,
      stableOffsets: [],
      variableOffsets: [],
      stableMask: "",
      windows: [],
      bestWindow: null,
    };
  }

  const stableByteFlags = Array.from({ length: comparableByteCount }, () => false);
  const stableOffsets = [];
  const variableOffsets = [];
  const stableMask = [];

  for (let offset = 0; offset < comparableByteCount; offset += 1) {
    const first = byteSamples[0][offset];
    const stable = byteSamples.every((pairs) => pairs[offset] === first);
    stableByteFlags[offset] = stable;
    if (stable) {
      stableOffsets.push(offset);
      stableMask.push(first);
    } else {
      variableOffsets.push(offset);
      stableMask.push("??");
    }
  }

  const supportFloor = Math.max(2, Math.ceil(byteSamples.length * 0.75));
  const windows = [];
  for (let offset = 0; offset < comparableByteCount - 1; offset += 1) {
    const pairCounts = new Map();
    for (const pairs of byteSamples) {
      const rawHex = `${pairs[offset]}${pairs[offset + 1]}`;
      pairCounts.set(rawHex, (pairCounts.get(rawHex) || 0) + 1);
    }
    const [rawHex, support] = [...pairCounts.entries()]
      .sort((left, right) => right[1] - left[1])[0] || [];
    if (!rawHex || !support || support < supportFloor) continue;

    const opcode = normalizeOpcode(`${rawHex.slice(2, 4)}${rawHex.slice(0, 2)}`);
    if (!opcode) continue;

    const leftNeighborStable = offset > 0 ? stableByteFlags[offset - 1] : true;
    const rightNeighborStable = offset + 2 < comparableByteCount ? stableByteFlags[offset + 2] : true;
    const isolatedScore = (leftNeighborStable ? 0 : 1) + (rightNeighborStable ? 0 : 1);
    const offsetBias = offset >= 2 && offset <= 12 ? 4 : 0;
    const leadingPenalty = offset === 0 ? 6 : offset === 1 ? 3 : 0;
    const rawPenalty = rawHex === "0000" || rawHex === "ffff" ? 4 : 0;
    const score = (support / byteSamples.length) * 100 + isolatedScore * 10 + offsetBias - leadingPenalty - rawPenalty;

    windows.push({
      offset,
      opcode,
      rawHex,
      support,
      sampleCount: byteSamples.length,
      stable: support === byteSamples.length,
      isolatedScore,
      score,
    });
  }

  const bestWindow = [...windows]
    .sort((left, right) => right.score - left.score || right.support - left.support || right.offset - left.offset)[0] || null;

  return {
    sampleCount: byteSamples.length,
    comparableByteCount,
    stableOffsets,
    variableOffsets,
    stableMask: stableMask.join(" "),
    windows,
    bestWindow,
  };
}

function getMatchingActivityItems(entry) {
  if (!entry) return [];
  return getActivityEntries().filter((item) => activityMatchesEntry(item, entry));
}

function getWorkflowSuggestedStage(entry, evidence, analysis) {
  if (entry.status === "validated" || analysis.bestCandidate?.opcode && entry.normalizedOpcode && analysis.bestCandidate.opcode === entry.normalizedOpcode) {
    return "confirmed";
  }
  if (analysis.bestCandidate && analysis.bestCandidate.count >= Math.max(2, Math.ceil(evidence.sessionCount * 0.5))) {
    return "opcode-hypothesis";
  }
  if (evidence.markerCount > 0 && evidence.sessionCount >= 3) {
    return "isolated";
  }
  if (evidence.sessionCount >= 3) {
    return "repeatable";
  }
  return "candidate";
}

function buildWorkflowAnalysis(entry) {
  const matches = getMatchingActivityItems(entry);
  const payloadAnalysis = analyzeWorkflowPayloads(matches);
  const routeCounts = new Map();
  const sourceCounts = new Map();
  const candidateStats = new Map();
  const responsePairStats = new Map();
  const sortedAscending = [...matches].sort((left, right) => {
    const leftTime = Number(left.timeEpoch || 0) || Date.parse(left.detectedUtc || "") || 0;
    const rightTime = Number(right.timeEpoch || 0) || Date.parse(right.detectedUtc || "") || 0;
    return leftTime - rightTime;
  });

  const addCount = (map, key, amount = 1) => {
    if (!key) return;
    map.set(key, (map.get(key) || 0) + amount);
  };

  for (let index = 0; index < sortedAscending.length; index += 1) {
    const item = sortedAscending[index];
    const routeKey = `${item.src || "?"}:${item.srcport || "?"} -> ${item.dst || "?"}:${item.dstport || "?"}`;
    addCount(routeCounts, routeKey);
    addCount(sourceCounts, item.analysisSource || "raw/unknown");

    const seenOpcodesForItem = new Set();
    for (const candidate of item.candidates || []) {
      const opcode = normalizeOpcode(candidate?.opcode);
      if (!opcode || seenOpcodesForItem.has(opcode)) continue;
      seenOpcodesForItem.add(opcode);
      if (!candidateStats.has(opcode)) {
        candidateStats.set(opcode, {
          opcode,
          names: new Set(),
          count: 0,
          offsets: new Map(),
          sources: new Map(),
        });
      }
      const stat = candidateStats.get(opcode);
      stat.count += 1;
      for (const name of candidate.names || []) stat.names.add(name);
      addCount(stat.offsets, String(candidate.offset ?? "?"));
      addCount(stat.sources, candidate.source || "raw");
    }

    const itemTime = Number(item.timeEpoch || 0) || Date.parse(item.detectedUtc || "") || 0;
    for (let probe = index + 1; probe < Math.min(sortedAscending.length, index + 6); probe += 1) {
      const other = sortedAscending[probe];
      const otherTime = Number(other.timeEpoch || 0) || Date.parse(other.detectedUtc || "") || 0;
      if (otherTime - itemTime > 0.35) break;
      if (
        String(item.src || "") === String(other.dst || "")
        && String(item.dst || "") === String(other.src || "")
        && String(item.srcport || "") === String(other.dstport || "")
        && String(item.dstport || "") === String(other.srcport || "")
      ) {
        const pairKey = `${item.srcport || "?"}->${item.dstport || "?"} / ${other.srcport || "?"}->${other.dstport || "?"}`;
        addCount(responsePairStats, pairKey);
        break;
      }
    }
  }

  const bestRoute = [...routeCounts.entries()].sort((left, right) => right[1] - left[1])[0] || null;
  const bestSource = [...sourceCounts.entries()].sort((left, right) => right[1] - left[1])[0] || null;
  const decodedCandidate = [...candidateStats.values()]
    .sort((left, right) => right.count - left.count || right.names.size - left.names.size)[0] || null;
  const bestPair = [...responsePairStats.entries()].sort((left, right) => right[1] - left[1])[0] || null;

  const evidence = {
    sessionCount: matches.length,
    markerCount: getLinkedMarkerCount(entry.id),
  };
  const bestCandidate = decodedCandidate
    ? {
      opcode: decodedCandidate.opcode,
      names: [...decodedCandidate.names],
      count: decodedCandidate.count,
      offset: [...decodedCandidate.offsets.entries()]
        .sort((left, right) => right[1] - left[1])[0]?.[0] || "?",
      source: [...decodedCandidate.sources.entries()]
        .sort((left, right) => right[1] - left[1])[0]?.[0] || "decoded",
      rationale: "decoded opcode candidate",
    }
    : payloadAnalysis.bestWindow
      ? {
        opcode: payloadAnalysis.bestWindow.opcode,
        names: [],
        count: payloadAnalysis.bestWindow.support,
        offset: payloadAnalysis.bestWindow.offset,
        source: "raw-window",
        rationale: `stable raw 16-bit window at byte offset ${payloadAnalysis.bestWindow.offset} (${payloadAnalysis.bestWindow.support}/${payloadAnalysis.bestWindow.sampleCount} packets)`,
      }
      : null;
  const suggestedStage = getWorkflowSuggestedStage(entry, evidence, { bestCandidate });

  const analysisLines = [
    `Matched packets this session: ${matches.length}`,
    bestRoute ? `Dominant route: ${bestRoute[0]} (${bestRoute[1]} hit${bestRoute[1] === 1 ? "" : "s"})` : "Dominant route: none yet",
    bestSource ? `Primary decode source: ${bestSource[0]} (${bestSource[1]} hit${bestSource[1] === 1 ? "" : "s"})` : "Primary decode source: none yet",
    bestPair ? `Likely request/response pair: ${bestPair[0]} (${bestPair[1]} time${bestPair[1] === 1 ? "" : "s"})` : "Likely request/response pair: not established",
  ];

  if (decodedCandidate) {
    const offsetSummary = [...decodedCandidate.offsets.entries()]
      .sort((left, right) => right[1] - left[1])
      .map(([offset, count]) => `offset ${offset} (${count})`)
      .join(", ");
    const sourceSummary = [...decodedCandidate.sources.entries()]
      .sort((left, right) => right[1] - left[1])
      .map(([source, count]) => `${source} (${count})`)
      .join(", ");
    analysisLines.push(
      `Suggested opcode hypothesis: ${decodedCandidate.opcode}${decodedCandidate.names.size ? ` • ${[...decodedCandidate.names].join(", ")}` : ""}`,
      `Hypothesis support: ${decodedCandidate.count}/${Math.max(matches.length, 1)} matching packets`,
      `Observed offsets: ${offsetSummary || "n/a"}`,
      `Observed decode sources: ${sourceSummary || "n/a"}`,
    );
  } else {
    if (payloadAnalysis.sampleCount >= 2) {
      analysisLines.push(
        `Comparable payload bytes: ${payloadAnalysis.comparableByteCount}`,
        `Stable byte mask: ${payloadAnalysis.stableMask || "n/a"}`,
        `Stable byte offsets: ${formatWorkflowOffsets(payloadAnalysis.stableOffsets)}`,
        `Variable byte offsets: ${formatWorkflowOffsets(payloadAnalysis.variableOffsets)}`,
      );
      if (payloadAnalysis.windows.length) {
        analysisLines.push(
          `Stable 16-bit windows: ${payloadAnalysis.windows
            .slice(0, 4)
            .map((window) => `${window.opcode} @ byte ${window.offset} (${window.support}/${window.sampleCount})`)
            .join(", ")}`,
        );
      } else {
        analysisLines.push("Stable 16-bit windows: none above the confidence threshold");
      }
      if (payloadAnalysis.bestWindow) {
        analysisLines.push(
          `Suggested opcode hypothesis: ${payloadAnalysis.bestWindow.opcode}`,
          `Hypothesis support: ${payloadAnalysis.bestWindow.support}/${payloadAnalysis.bestWindow.sampleCount} matching packets`,
          `Observed offset: byte ${payloadAnalysis.bestWindow.offset}`,
          `Observed decode source: raw stable window`,
        );
      } else {
        analysisLines.push("Suggested opcode hypothesis: none yet");
      }
    } else {
      analysisLines.push("Suggested opcode hypothesis: none yet", "Repeated payload comparison: need at least 2 matched packets with captured payload data");
    }
  }

  return {
    evidence,
    matches,
    payloadAnalysis,
    bestCandidate,
    bestRoute,
    bestSource,
    bestPair,
    suggestedStage,
    summary: analysisLines.join("\n"),
  };
}

function getSessionEntryCounts() {
  const activityEntries = getActivityEntries();
  const detectionEntries = state.liveSession.detections || [];
  const sessionVersionKey = getSessionKey(state.liveSession);
  if (
    runtimeCache.sessionEntryCountVersionKey === sessionVersionKey
    && runtimeCache.sessionEntryCountActivityRef === activityEntries
    && runtimeCache.sessionEntryCountDetectionsRef === detectionEntries
    && runtimeCache.sessionEntryCountEntryVersion === runtimeCache.entryVersion
    && runtimeCache.sessionEntryCountPreferenceVersion === runtimeCache.preferenceVersion
  ) {
    return runtimeCache.sessionEntryCounts;
  }

  getAllEntries();
  const counts = new Map();
  const customEntries = runtimeCache.allEntries.filter((entry) => entry.source_type === "custom");
  for (const item of activityEntries) {
    const matchedEntryIds = collectEntryIdsForCountItem(item, customEntries);
    for (const entryId of matchedEntryIds) {
      counts.set(entryId, (counts.get(entryId) || 0) + 1);
    }
  }

  const seenActivityIds = new Set(activityEntries.map((item) => item?.id).filter(Boolean));
  for (const detection of detectionEntries) {
    if (!detection) continue;
    if (detection.id && seenActivityIds.has(detection.id)) continue;
    const matchedEntryIds = collectEntryIdsForCountItem(detection, customEntries);
    for (const entryId of matchedEntryIds) {
      counts.set(entryId, (counts.get(entryId) || 0) + 1);
    }
  }

  runtimeCache.sessionEntryCounts = counts;
  runtimeCache.sessionEntryCountVersionKey = sessionVersionKey;
  runtimeCache.sessionEntryCountActivityRef = activityEntries;
  runtimeCache.sessionEntryCountDetectionsRef = detectionEntries;
  runtimeCache.sessionEntryCountEntryVersion = runtimeCache.entryVersion;
  runtimeCache.sessionEntryCountPreferenceVersion = runtimeCache.preferenceVersion;
  return counts;
}

function syncSessionOpcodeCountPulse() {
  const sessionKey = getSessionKey(state.liveSession);
  const counts = getSessionEntryCounts();

  if (sessionKey !== state.countPulseSessionKey) {
    state.countPulseSessionKey = sessionKey;
    state.lastSessionEntryCounts = new Map(counts);
    clearRecentCountTimers();
    return [];
  }

  const changedEntryIds = [];
  for (const [entryId, nextCount] of counts.entries()) {
    const previousCount = state.lastSessionEntryCounts.get(entryId) || 0;
    if (nextCount <= previousCount) continue;
    changedEntryIds.push(entryId);
  }

  state.lastSessionEntryCounts = new Map(counts);
  if (changedEntryIds.length) {
    markEntriesCountRecentlyChanged(changedEntryIds);
  }
  return changedEntryIds;
}

function collectEntryIdsForCountItem(item, customEntries = runtimeCache.allEntries.filter((entry) => entry.source_type === "custom")) {
  const matchedEntryIds = new Set();
  const groupKey = normalizePacketFamilyKey(item?.groupKey);
  if (groupKey && runtimeCache.entriesByPacketFamilyKey.has(groupKey)) {
    for (const entry of runtimeCache.entriesByPacketFamilyKey.get(groupKey) || []) {
      matchedEntryIds.add(entry.id);
    }
  }

  const opcodes = getDetectionOpcodes(item);
  if (opcodes.size) {
    for (const opcode of opcodes) {
      const normalized = normalizeOpcode(opcode);
      if (!normalized || !runtimeCache.entriesByOpcode.has(normalized)) continue;
      for (const entry of runtimeCache.entriesByOpcode.get(normalized) || []) {
        matchedEntryIds.add(entry.id);
      }
    }
  }

  const payloadPrefix = normalizePacketSignature(item?.payloadPrefix);
  if (payloadPrefix) {
    for (const [signature, entries] of runtimeCache.entriesByPacketSignature.entries()) {
      if (!signature || !payloadPrefix.startsWith(signature)) continue;
      for (const entry of entries) {
        matchedEntryIds.add(entry.id);
      }
    }
  }

  const customPacketAlias = getPacketPreference(item).alias;
  const detectionLabelKey = normalizeLabelKey(customPacketAlias || getDetectionDisplayName(item));
  if (detectionLabelKey.length >= 6) {
    for (const entry of customEntries) {
      const entryLabelKey = normalizeLabelKey(getEntryDisplayName(entry) || entry.display_name || "");
      if (entry.normalizedOpcode || entryLabelKey.length < 6) continue;
      if (detectionLabelKey.includes(entryLabelKey) || entryLabelKey.includes(detectionLabelKey)) {
        matchedEntryIds.add(entry.id);
      }
    }
  }

  return matchedEntryIds;
}

function activityMatchesEntry(item, entry) {
  if (!item || !entry) return false;

  if (entry.normalizedPacketFamilyKey) {
    const groupKey = normalizePacketFamilyKey(item.groupKey);
    if (groupKey && groupKey === entry.normalizedPacketFamilyKey) return true;
  }

  if (entry.normalizedOpcode) {
    const opcodes = getDetectionOpcodes(item);
    for (const opcode of opcodes) {
      if (normalizeOpcode(opcode) === entry.normalizedOpcode) return true;
    }
  }

  if (entry.normalizedPacketSignature) {
    const payloadPrefix = normalizePacketSignature(item.payloadPrefix);
    if (payloadPrefix && payloadPrefix.startsWith(entry.normalizedPacketSignature)) return true;
  }

  if (!entry.normalizedOpcode) {
    const customPacketAlias = getPacketPreference(item).alias;
    const detectionLabelKey = normalizeLabelKey(customPacketAlias || getDetectionDisplayName(item));
    const entryLabelKey = normalizeLabelKey(getEntryDisplayName(entry) || entry.display_name || "");
    if (
      detectionLabelKey.length >= 6
      && entryLabelKey.length >= 6
      && (detectionLabelKey.includes(entryLabelKey) || entryLabelKey.includes(detectionLabelKey))
    ) {
      return true;
    }
  }

  return false;
}

function entryMatchesFilters(entry) {
  const search = state.filters.search.toLowerCase();
  if (search) {
    const haystack = [
      entry.rof2_opcode,
      entry.rof2_name,
      entry.eqemu_name,
      entry.notes,
      entry.extra,
      entry.userNotes,
      entry.tags,
      String(entry.sheet_row || ""),
    ].join(" ").toLowerCase();
    if (!haystack.includes(search)) return false;
  }

  if (state.filters.status !== "all" && entry.status !== state.filters.status) return false;
  if (state.filters.source !== "all" && entry.source_type !== state.filters.source) return false;
  if (state.filters.trackedOnly && !entry.tracked) return false;
  return true;
}

function compareEntries(left, right) {
  switch (state.filters.sort) {
    case "recent": {
      const timestampCompare = applyDirection((Date.parse(left.updatedAt || "") || 0) - (Date.parse(right.updatedAt || "") || 0));
      return timestampCompare
        || compareBoolean(right.tracked, left.tracked)
        || compareOpcode(left, right);
    }
    case "alert": {
      const alertCompare = applyDirection(compareBoolean(left.alertEnabled, right.alertEnabled));
      return alertCompare
        || compareBoolean(right.tracked, left.tracked)
        || compareOpcode(left, right);
    }
    case "count": {
      const entryCounts = getSessionEntryCounts();
      const leftCount = entryCounts.get(left.id) || 0;
      const rightCount = entryCounts.get(right.id) || 0;
      return applyDirection(leftCount - rightCount)
        || compareBoolean(right.alertEnabled, left.alertEnabled)
        || compareOpcode(left, right);
    }
    case "opcode":
      return applyDirection(compareOpcode(left, right))
        || compareText(getEntryDisplayName(left), getEntryDisplayName(right));
    case "name":
      return applyDirection(compareText(getEntryDisplayName(left), getEntryDisplayName(right)))
        || compareOpcode(left, right);
    case "eqemu":
      return applyDirection(compareText(left.eqemu_name, right.eqemu_name))
        || compareText(getEntryDisplayName(left), getEntryDisplayName(right))
        || compareOpcode(left, right);
    case "status":
      return applyDirection(statusRank(left.status) - statusRank(right.status))
        || compareBoolean(right.tracked, left.tracked)
        || compareOpcode(left, right);
    case "source":
      return applyDirection(compareText(getSourceLabel(left.source_type), getSourceLabel(right.source_type)))
        || compareText(getEntryDisplayName(left), getEntryDisplayName(right))
        || compareOpcode(left, right);
    case "notes":
      return applyDirection(compareText(getEntryNoteText(left), getEntryNoteText(right)))
        || compareText(getEntryDisplayName(left), getEntryDisplayName(right))
        || compareOpcode(left, right);
    case "tracked":
    default:
      return applyDirection(compareBoolean(left.tracked, right.tracked))
        || statusRank(left.status) - statusRank(right.status)
        || Number(Boolean(right.rof2_name || right.eqemu_name)) - Number(Boolean(left.rof2_name || left.eqemu_name))
        || compareOpcode(left, right);
  }
}

function getVisibleEntries() {
  return getAllEntries().filter(entryMatchesFilters).sort(compareEntries);
}

function getBookmarkId(marker, fallbackIndex = 0) {
  return `${marker.MarkedUtc || marker.MarkedEpoch || "marker"}|${marker.Label || "bookmark"}|${fallbackIndex}`;
}

function getBookmarks() {
  return (state.liveSession.markers || []).map((marker, index) => {
    const id = getBookmarkId(marker, index);
    const saved = state.bookmarkState[id] || {};
    return {
      ...marker,
      id,
      expectedOpcode: saved.expectedOpcode || "",
      notes: saved.notes || "",
      linkedEntryId: saved.linkedEntryId || "",
    };
  }).sort((left, right) => (right.MarkedEpoch || 0) - (left.MarkedEpoch || 0));
}

  function getBookmarkById(id) {
    return getBookmarks().find((bookmark) => bookmark.id === id) || null;
  }

  function clearCurrentBookmarkState() {
    const bookmarkIds = new Set(getBookmarks().map((bookmark) => bookmark.id));
    if (!bookmarkIds.size) return;
    for (const id of bookmarkIds) {
      delete state.bookmarkState[id];
    }
  }

function getPaginationMeta(entries) {
  const totalPages = Math.max(1, Math.ceil(entries.length / state.pagination.pageSize));
  const page = Math.min(state.pagination.page, totalPages);
  const startIndex = entries.length ? (page - 1) * state.pagination.pageSize : 0;
  const endIndex = Math.min(startIndex + state.pagination.pageSize, entries.length);
  return {
    page,
    totalPages,
    startIndex,
    endIndex,
    pageEntries: entries.slice(startIndex, endIndex),
  };
}

function getBookmarkPaginationMeta(bookmarks) {
  const totalPages = Math.max(1, Math.ceil(bookmarks.length / state.bookmarkPagination.pageSize));
  const page = Math.min(state.bookmarkPagination.page, totalPages);
  const startIndex = bookmarks.length ? (page - 1) * state.bookmarkPagination.pageSize : 0;
  const endIndex = Math.min(startIndex + state.bookmarkPagination.pageSize, bookmarks.length);
  return {
    page,
    totalPages,
    startIndex,
    endIndex,
    pageBookmarks: bookmarks.slice(startIndex, endIndex),
  };
}

function getRequestedSessionName() {
  return els.sessionNameInput.value.trim() || state.liveSession.sessionName || "rof2-live-ui";
}

function sanitizeSessionNameSegment(value) {
  return String(value || "")
    .trim()
    .replace(/[^\w.-]+/g, "-")
    .replace(/-+/g, "-")
    .replace(/^-|-$/g, "");
}

function getSessionRestartTimestamp() {
  const now = new Date();
  const pad = (value) => String(value).padStart(2, "0");
  return [
    now.getFullYear(),
    pad(now.getMonth() + 1),
    pad(now.getDate()),
    "-",
    pad(now.getHours()),
    pad(now.getMinutes()),
    pad(now.getSeconds()),
  ].join("");
}

function stripSessionTimestampSuffix(value) {
  return String(value || "").replace(/-\d{8}-\d{6}$/, "");
}

function buildUniqueSessionName(sourceName = "rof2-live-ui") {
  const fallbackName = sanitizeSessionNameSegment(sourceName) || "rof2-live-ui";
  const trimmedBase = stripSessionTimestampSuffix(fallbackName);
  return `${trimmedBase}-${getSessionRestartTimestamp()}`;
}

function forceSeedNextSessionName(baseName = "rof2-live-ui") {
  const nextName = buildUniqueSessionName(baseName);
  state.sessionNameSeed = nextName;
  els.sessionNameInput.value = nextName;
}

function maybeSeedNextSessionName(baseName = "rof2-live-ui") {
  const currentValue = els.sessionNameInput.value.trim();
  if (currentValue && currentValue !== state.sessionNameSeed) {
    return;
  }

  forceSeedNextSessionName(baseName);
}

function getCurrentInterfaceSelection() {
  return els.sessionInterfaceInput.value || state.liveSession.interface || getPreferredCaptureInterfaceValue();
}

function getPreferredCaptureInterfaceValue(explicitInterfaces = state.interfaces) {
  const interfaces = Array.isArray(explicitInterfaces) ? explicitInterfaces : [];
  const ethernetMatch = interfaces.find((item) => {
    const value = String(item?.value || "").trim().toLowerCase();
    const label = String(item?.label || "").trim().toLowerCase();
    return value === "ethernet" || label === "ethernet";
  });
  if (ethernetMatch?.value) return ethernetMatch.value;

  const nonLoopbackMatch = interfaces.find((item) => {
    const value = String(item?.value || "").trim().toLowerCase();
    const label = String(item?.label || "").trim().toLowerCase();
    return value && value !== "loopback" && label !== "loopback";
  });
  if (nonLoopbackMatch?.value) return nonLoopbackMatch.value;

  return "loopback";
}

function ensureInterfaceOption(value, label = value, description = "") {
  const normalized = (value || "").trim();
  if (!normalized) return;
  const existing = [...els.sessionInterfaceInput.options].find((option) => option.value === normalized);
  if (existing) {
    if (description) existing.title = description;
    return;
  }

  const option = document.createElement("option");
  option.value = normalized;
  option.textContent = label || normalized;
  if (description) option.title = description;
  els.sessionInterfaceInput.appendChild(option);
}

function renderInterfaceOptions(selectedValue = getCurrentInterfaceSelection()) {
  const fallbackInterfaces = state.interfaces.length
    ? state.interfaces
    : [{ value: "loopback", label: "Loopback", description: "Adapter for loopback traffic capture" }];

  els.sessionInterfaceInput.innerHTML = "";
  for (const item of fallbackInterfaces) {
    ensureInterfaceOption(item.value, item.label, item.description);
  }

  const normalizedSelected = (selectedValue || "").trim() || getPreferredCaptureInterfaceValue(fallbackInterfaces);
  const matched = fallbackInterfaces.find((item) => item.value === normalizedSelected);
  if (!matched) {
    ensureInterfaceOption(normalizedSelected, normalizedSelected, "Currently active session interface");
  }
  els.sessionInterfaceInput.value = normalizedSelected;
}

function getRequestedMarkerLabel() {
  return els.sessionMarkerLabelInput.value.trim() || "marker";
}

function isLikelyEqPort(portValue) {
  const port = Number(portValue || 0);
  if (!Number.isFinite(port) || port <= 0) return false;
  return port === 5998 || port === 5999 || port === 7778 || (port >= 7000 && port <= 7999);
}

function confidenceRank(value) {
  return { none: 0, low: 1, medium: 2, high: 3 }[value || "none"] || 0;
}

function deriveEqLike(item) {
  const explicitConfidence = item.eqConfidence || "none";
  if (confidenceRank(explicitConfidence) >= 2) {
    return {
      eqLike: true,
      eqLikeReason: item.eqConfidenceReason || item.eqLikeReason || "",
      eqConfidence: explicitConfidence,
      eqConfidenceReason: item.eqConfidenceReason || "",
      flowTrusted: Boolean(item.flowTrusted),
    };
  }
  if (item.eqLike) {
    return {
      eqLike: true,
      eqLikeReason: item.eqLikeReason || "",
      eqConfidence: explicitConfidence === "none" ? "medium" : explicitConfidence,
      eqConfidenceReason: item.eqConfidenceReason || item.eqLikeReason || "",
      flowTrusted: Boolean(item.flowTrusted),
    };
  }
  if (item.ignored) {
    return { eqLike: false, eqLikeReason: "", eqConfidence: "none", eqConfidenceReason: "", flowTrusted: false };
  }
  if (isLikelyEqPort(item.srcport) || isLikelyEqPort(item.dstport)) {
    const matchedPort = isLikelyEqPort(item.srcport) ? item.srcport : item.dstport;
    return {
      eqLike: true,
      eqLikeReason: `likely-eq-port:${matchedPort}`,
      eqConfidence: "medium",
      eqConfidenceReason: `likely-eq-port:${matchedPort}`,
      flowTrusted: false,
    };
  }
  if ((item.analysisSource || "").startsWith("zlib@")) {
    return {
      eqLike: true,
      eqLikeReason: "compressed-opcode",
      eqConfidence: "medium",
      eqConfidenceReason: "compressed-opcode",
      flowTrusted: false,
    };
  }
  if ((item.info || "").toUpperCase().includes("ABORT")) {
    return {
      eqLike: false,
      eqLikeReason: "",
      eqConfidence: "low",
      eqConfidenceReason: "abort-without-trust",
      flowTrusted: false,
    };
  }
  return {
    eqLike: false,
    eqLikeReason: "",
    eqConfidence: item.hasCandidates ? "low" : "none",
    eqConfidenceReason: item.hasCandidates ? "raw-candidate-only" : "",
    flowTrusted: false,
  };
}

function getActivityEntries() {
  const activityRef = state.liveSession.activity || [];
  const activityVersionKey = getSessionKey(state.liveSession);
  if (
    runtimeCache.activityVersionKey === activityVersionKey
    && runtimeCache.activityRef === activityRef
    && runtimeCache.activityEntryVersion === runtimeCache.entryVersion
    && runtimeCache.activityPreferenceVersion === runtimeCache.preferenceVersion
    && runtimeCache.activitySuppressionVersion === runtimeCache.suppressionVersion
  ) {
    return runtimeCache.activityEntries;
  }

  const activityEntries = activityRef.map((item) => {
    const matchedEntries = matchDetectionToEntries(item);
    const eqLikeMeta = deriveEqLike(item);
    const opcodeKnowledge = getOpcodeKnowledgeMeta(item);
    const opcodePreference = getDetectionPreference(item);
    const alertLabels = getDetectionAlertLabels({ ...item, matchedEntries });
    const decoratedItem = {
      ...item,
      matchedEntries,
      opcodeAlias: opcodePreference.alias,
      opcodeFlagged: opcodePreference.flagged,
      displayName: opcodePreference.alias || getDefaultDetectionLabel({ ...item, matchedEntries }),
      alertLabels,
      ignored: Boolean(item.ignored),
      hasCandidates: Boolean(item.hasCandidates || item.candidateCount || item.opcode || (item.candidates || []).length),
      hasFlaggedMatches: matchedEntries.length > 0 || opcodePreference.flagged,
      eqLike: eqLikeMeta.eqLike,
      eqLikeReason: eqLikeMeta.eqLikeReason,
      eqConfidence: eqLikeMeta.eqConfidence,
      eqConfidenceReason: eqLikeMeta.eqConfidenceReason,
      flowTrusted: eqLikeMeta.flowTrusted,
      knownOpcode: opcodeKnowledge.knownOpcode,
      unknownOpcode: opcodeKnowledge.unknownOpcode,
      onlyKnownOpcode: opcodeKnowledge.onlyKnownOpcode,
    };
    decoratedItem.groupKey = getLiveMonitorGroupKey(decoratedItem);
    const packetPreference = opcodePreference.alias ? { alias: "" } : getPacketPreference(decoratedItem);
    if (!decoratedItem.opcodeAlias && packetPreference.alias) {
      decoratedItem.opcodeAlias = packetPreference.alias;
      decoratedItem.displayName = packetPreference.alias;
    }
    const suppressionRule = getSuppressionRuleForItem(decoratedItem);
    decoratedItem.suppressionRule = suppressionRule;
    decoratedItem.suppressed = Boolean(suppressionRule);
    return decoratedItem;
  }).sort((left, right) => {
    const rightTime = right.timeEpoch || Date.parse(right.detectedUtc || "") || 0;
    const leftTime = left.timeEpoch || Date.parse(left.detectedUtc || "") || 0;
    return rightTime - leftTime;
  });
  runtimeCache.activityEntries = activityEntries;
  runtimeCache.activityById = new Map(activityEntries.map((item) => [item.id, item]));
  runtimeCache.activityVersionKey = activityVersionKey;
  runtimeCache.activityRef = activityRef;
  runtimeCache.activityEntryVersion = runtimeCache.entryVersion;
  runtimeCache.activityPreferenceVersion = runtimeCache.preferenceVersion;
  runtimeCache.activitySuppressionVersion = runtimeCache.suppressionVersion;
  return activityEntries;
}

function getLiveMonitorPriority(item) {
  if (item.hasFlaggedMatches) return 0;
  if (item.hasCandidates) return 1;
  if (item.eqConfidence === "high") return 2;
  if (item.eqConfidence === "medium") return 3;
  if (item.eqConfidence === "low") return 4;
  if (item.eqLike) return 5;
  if (item.ignored) return 7;
  return 6;
}

function sortLiveMonitorItems(items) {
  return [...items].sort((left, right) => {
    const priorityDelta = getLiveMonitorPriority(left) - getLiveMonitorPriority(right);
    if (priorityDelta !== 0) return priorityDelta;

    const rightTime = right.timeEpoch || Date.parse(right.detectedUtc || "") || 0;
    const leftTime = left.timeEpoch || Date.parse(left.detectedUtc || "") || 0;
    if (rightTime !== leftTime) return rightTime - leftTime;

    return Number(right.frameNumber || 0) - Number(left.frameNumber || 0);
  });
}

function getLiveMonitorGroupKey(item) {
  const normalizedOpcode = normalizeOpcode(item.opcode);
  if (normalizedOpcode) {
    return `opcode:${normalizedOpcode}|label:${item.displayName || ""}`;
  }

  const candidateOpcodes = (item.candidates || [])
    .map((candidate) => normalizeOpcode(candidate.opcode))
    .filter(Boolean)
    .join(",");
  if (candidateOpcodes) {
    return `candidates:${candidateOpcodes}|route:${item.srcport || "?"}->${item.dstport || "?"}|len:${item.length || "?"}`;
  }

  const info = normalizeMonitorText(item.info || "").slice(0, 72).toLowerCase();
  const payloadPrefix = String(item.payloadPrefix || "").slice(0, 24).toLowerCase();
  if (item.eqLike) {
    return `eq:${item.eqLikeReason || "eq"}|route:${item.srcport || "?"}->${item.dstport || "?"}|len:${item.length || "?"}|info:${info}`;
  }
  if (item.ignored) {
    return `noise:${item.ignoredReason || "ignored"}|route:${item.srcport || "?"}->${item.dstport || "?"}|len:${item.length || "?"}|info:${info}`;
  }
  return `traffic:${item.srcport || "?"}->${item.dstport || "?"}|len:${item.length || "?"}|info:${info}|payload:${payloadPrefix}`;
}

function groupLiveMonitorItems(items) {
  const grouped = [];
  const byKey = new Map();
  for (const item of items) {
    const key = getLiveMonitorGroupKey(item);
    if (!byKey.has(key)) {
      const aggregate = {
        ...item,
        groupKey: key,
        feedCount: 1,
        recentInGroup: state.recentActivityIds.has(item.id),
      };
      byKey.set(key, aggregate);
      grouped.push(aggregate);
      continue;
    }

    const aggregate = byKey.get(key);
    aggregate.feedCount += 1;
    aggregate.recentInGroup = aggregate.recentInGroup || state.recentActivityIds.has(item.id);
    if (!aggregate.hasFlaggedMatches && item.hasFlaggedMatches) aggregate.hasFlaggedMatches = true;
    if (!aggregate.opcode && item.opcode) aggregate.opcode = item.opcode;
    if (!aggregate.opcodeAlias && item.opcodeAlias) aggregate.opcodeAlias = item.opcodeAlias;
    if (!aggregate.analysisSource && item.analysisSource) aggregate.analysisSource = item.analysisSource;
    if (!aggregate.payloadPrefix && item.payloadPrefix) aggregate.payloadPrefix = item.payloadPrefix;
    if ((!aggregate.matchedEntries || !aggregate.matchedEntries.length) && item.matchedEntries?.length) {
      aggregate.matchedEntries = item.matchedEntries;
    }
    if ((!aggregate.candidates || !aggregate.candidates.length) && item.candidates?.length) {
      aggregate.candidates = item.candidates;
    }
  }
  return grouped;
}

function getLiveMonitorCountLimit() {
  const raw = String(state.liveMonitorFilters.countLimit || "").trim();
  if (!raw) return 0;
  const parsed = Number(raw);
  if (!Number.isFinite(parsed) || parsed < 1) return 0;
  return Math.floor(parsed);
}

function activityIsAfterBaseline(item, baseline = state.liveMonitorFeedBaseline) {
  if (!baseline?.sessionKey || baseline.sessionKey !== getSessionKey(state.liveSession)) return true;
  const frameNumber = Number(item?.frameNumber || 0);
  if (frameNumber && frameNumber > Number(baseline.frameNumber || 0)) return true;
  if (frameNumber && frameNumber <= Number(baseline.frameNumber || 0)) return false;
  const timeEpoch = Number(item?.timeEpoch || 0);
  return timeEpoch > Number(baseline.timeEpoch || 0);
}

function getLiveMonitorCursor(live = state.liveSession) {
  const session = live || {};
  let frameNumber = Number(session.lastFrameNumber || 0);
  let timeEpoch = Date.parse(session.syncedUtc || "") || 0;
  for (const item of session.activity || []) {
    const itemFrame = Number(item?.frameNumber || 0);
    if (itemFrame > frameNumber) frameNumber = itemFrame;
    const itemTime = Number(item?.timeEpoch || 0) || Date.parse(item?.detectedUtc || "") || 0;
    if (itemTime > timeEpoch) timeEpoch = itemTime;
  }
  return {
    sessionKey: getSessionKey(session),
    frameNumber,
    timeEpoch,
  };
}

function resetLiveMonitorFeedState() {
  state.liveMonitorFeedBaseline = { sessionKey: "", frameNumber: 0, timeEpoch: 0 };
  clearRecentActivityTimers();
  state.currentAlertMatches = [];
  state.recentDetectedEntryIds = new Set();
  renderDetectionAlertModal();
}

async function clearLiveMonitorFeed() {
  if (!state.liveSession.sessionName) {
    resetLiveMonitorFeedState();
    renderLiveMonitor();
    return;
  }

  const previousCursor = getLiveMonitorCursor();
  await runSessionAction("clearDetections");
  const clearedCursor = getLiveMonitorCursor();
  state.liveMonitorFeedBaseline = {
    sessionKey: clearedCursor.sessionKey || previousCursor.sessionKey,
    frameNumber: Math.max(previousCursor.frameNumber, clearedCursor.frameNumber),
    timeEpoch: Math.max(previousCursor.timeEpoch, clearedCursor.timeEpoch),
  };
  clearRecentActivityTimers();
  state.currentAlertMatches = [];
  state.recentDetectedEntryIds = new Set();
  renderDetectionAlertModal();
  await refreshLiveSession();
  renderLiveMonitor();
}

function activityMatchesFilters(item) {
  if (state.liveMonitorFilters.unknownOnly && item.onlyKnownOpcode) return false;
  if (state.liveMonitorFilters.mode === "eqlike" && !item.eqLike) return false;
  if (state.liveMonitorFilters.mode === "candidates" && !item.hasCandidates) return false;
  if (state.liveMonitorFilters.mode === "flagged" && !item.hasFlaggedMatches) return false;

  const search = state.liveMonitorFilters.search.trim().toLowerCase();
  if (!search) return true;

  const haystack = [
    item.opcode,
    item.analysisSource,
    item.info,
    item.payloadPrefix,
    item.src,
    item.srcport,
    item.dst,
    item.dstport,
    item.length,
    item.frameNumber,
    item.eqLikeReason,
    item.eqConfidence,
    item.eqConfidenceReason,
    item.opcodeAlias,
    item.displayName,
    item.names?.join(" "),
    item.alertLabels?.join(" "),
    item.matchedEntries?.map((entry) => [entry.rof2_opcode, entry.rof2_name, entry.eqemu_name].filter(Boolean).join(" ")).join(" "),
    item.candidates?.map((candidate) => [candidate.opcode, candidate.name].filter(Boolean).join(" ")).join(" "),
  ].join(" ").toLowerCase();

  return haystack.includes(search);
}

function getSuppressedActivityEntries(items = getActivityEntries()) {
  return sortLiveMonitorItems(items.filter((item) => item.suppressed));
}

function suppressionRuleMatchesSearch(rule, search) {
  const query = search.trim().toLowerCase();
  if (!query) return true;
  return [
    rule.label,
    rule.matcherLabel,
    rule.type,
    rule.opcode,
    rule.signature,
  ].join(" ").toLowerCase().includes(query);
}

function applyLiveSessionPayload(payload) {
  state.liveSession = {
    status: payload.status || "idle",
    sessionName: payload.sessionName || "",
    interface: payload.interface || "",
    resolvedInterface: payload.resolvedInterface || "",
    captureFilter: payload.captureFilter || "",
    client: payload.client || "RoF2",
    capturePath: payload.capturePath || "",
    markersPath: payload.markersPath || "",
    detectionsPath: payload.detectionsPath || "",
    startedUtc: payload.startedUtc || "",
    stoppedUtc: payload.stoppedUtc || "",
    markerCount: payload.markerCount || 0,
    markers: payload.markers || [],
    activityCount: payload.activityCount || 0,
    activity: payload.activity || [],
    detectionCount: payload.detectionCount || 0,
    detections: payload.detections || [],
    lastFrameNumber: payload.lastFrameNumber || 0,
    syncedUtc: payload.syncedUtc || "",
  };
}

async function runSessionAction(action, extra = {}) {
  const normalizedAction = String(action || "").trim().toLowerCase();
  const supportedActions = new Set(["start", "stop", "mark", "clearmarkers", "cleardetections"]);
  if (!supportedActions.has(normalizedAction)) {
    setSessionActionMessage(`Ignored unsupported local session action: ${action || "(empty)"}.`, "error");
    return;
  }

  state.sessionActionPending = true;
  renderLiveSession();
  setSessionActionMessage(`${normalizedAction} in progress...`);

  const payload = {
    action: normalizedAction,
    sessionName: getRequestedSessionName(),
    client: "RoF2",
    interface: getCurrentInterfaceSelection(),
    captureFilter: els.sessionCaptureFilterInput.value.trim() || "udp",
    durationSec: Number(els.sessionDurationInput.value) || 0,
    label: getRequestedMarkerLabel(),
    note: els.sessionMarkerNoteInput.value.trim(),
    ...extra,
  };

  try {
    const controller = new AbortController();
    const timeoutId = window.setTimeout(() => controller.abort(), 12000);
    const response = await fetch(API_SESSION_URL, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      signal: controller.signal,
      body: JSON.stringify(payload),
    });
    window.clearTimeout(timeoutId);
    const result = await response.json().catch(() => ({}));
    if (!response.ok || result.ok === false) {
      throw new Error(result.error || result.message || `Session action failed with HTTP ${response.status}`);
    }

    if (result.session) {
      applyLiveSessionPayload(result.session);
      await processLiveDetections();
    } else {
      await refreshLiveSession();
    }

    if (state.liveSession.sessionName) {
      els.sessionNameInput.value = state.liveSession.sessionName;
      state.sessionNameSeed = "";
    }

    if (normalizedAction === "mark") {
      state.bookmarkPagination.page = 1;
      state.selectedBookmarkId = getBookmarks()[0]?.id || state.selectedBookmarkId;
      els.sessionMarkerLabelInput.value = "marker";
      els.sessionMarkerNoteInput.value = "";
    } else if (normalizedAction === "start") {
      state.filters.sort = "count";
      state.filters.sortDirection = "desc";
      state.pagination.page = 1;
      syncSortUi();
    } else if (normalizedAction === "cleardetections") {
      state.seenDetectionIds = new Set((state.liveSession.detections || []).map((detection) => detection.id));
      state.seenActivityIds = new Set((state.liveSession.activity || []).map((item) => item.id).filter(Boolean));
      resetLiveMonitorFeedState();
    } else if (normalizedAction === "stop") {
      forceSeedNextSessionName(stripSessionTimestampSuffix(payload.sessionName || state.liveSession.sessionName || "rof2-live-ui"));
    }

    renderLiveSession();
    renderBookmarks();
    renderBookmarkModal();
    setSessionActionMessage(result.message || `${normalizedAction} completed.`, "success");
  } catch (error) {
    if (error.name === "AbortError") {
      await refreshLiveSession();
      const recovered = normalizedAction === "start"
        ? state.liveSession.status === "running" && state.liveSession.sessionName === payload.sessionName
        : normalizedAction === "stop"
          ? state.liveSession.sessionName === payload.sessionName && state.liveSession.status !== "running"
          : normalizedAction === "mark"
            ? state.liveSession.sessionName === payload.sessionName && state.liveSession.status === "running"
            : false;
      if (recovered) {
        setSessionActionMessage(`${normalizedAction} completed after a slow bridge response.`, "success");
      } else {
        setSessionActionMessage("The OCC bridge timed out before it confirmed the session action.", "error");
      }
    } else {
      setSessionActionMessage(error.message, "error");
    }
  } finally {
    state.sessionActionPending = false;
    renderLiveSession();
  }
}

async function openCapturesFolder() {
  const previousMessage = els.sessionActionMessage.textContent;
  const previousTone = els.sessionActionMessage.dataset.tone || "muted";
  setSessionActionMessage("Opening captures folder...", "muted");
  try {
    const response = await fetch(API_OPEN_CAPTURES_URL, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: "{}",
    });
    const result = await response.json().catch(() => ({}));
    if (!response.ok || result.ok === false) {
      throw new Error(result.error || `Open folder failed with HTTP ${response.status}`);
    }
    setSessionActionMessage(`Opened captures folder: ${result.path}`, "success");
  } catch (error) {
    setSessionActionMessage(error.message || previousMessage, "error");
    return;
  }
  window.setTimeout(() => {
    if (els.sessionActionMessage.textContent.startsWith("Opened captures folder:")) {
      setSessionActionMessage(previousMessage, previousTone);
    }
  }, 3500);
}

function renderStats() {
  const all = getAllEntries();
  const tracked = all.filter((entry) => entry.tracked).length;
  const named = all.filter((entry) => entry.rof2_name || entry.eqemu_name).length;
  const validated = all.filter((entry) => entry.status === "validated").length;
  const mismatches = all.filter((entry) => entry.alignment === "mismatch").length;
  const stats = [
    ["Total Rows", all.length],
    ["Named", named],
    ["Tracked", tracked],
    ["Validated", validated],
    ["Name Mismatches", mismatches],
  ];

  els.statsGrid.innerHTML = "";
  for (const [label, value] of stats) {
    const card = document.createElement("div");
    card.className = "panel stat-card";
    card.innerHTML = `<span class="stat-value">${value}</span><span class="stat-label">${label}</span>`;
    els.statsGrid.appendChild(card);
  }
}

async function restartCaptureSession() {
  const live = state.liveSession || {};
  if (live.status !== "running" || !live.sessionName) {
    throw new Error("Restart is only available while a capture session is running.");
  }

  const nextSessionName = live.sessionName;
  setSessionActionMessage(`Restarting capture ${nextSessionName}...`);
  await runSessionAction("stop", { sessionName: live.sessionName });
  els.sessionNameInput.value = nextSessionName;
  await runSessionAction("start", { sessionName: nextSessionName });
  state.sessionNameSeed = "";
  setSessionActionMessage(`Capture restarted as ${nextSessionName}.`, "success");
}

function renderTable() {
  const entries = getVisibleEntries();
  const entryCounts = getSessionEntryCounts();
  const { page, totalPages, startIndex, endIndex, pageEntries } = getPaginationMeta(entries);
  state.pagination.page = page;
  syncSortUi();
  els.opcodeTableBody.innerHTML = "";
  els.resultsSummary.textContent = `${entries.length} visible of ${getAllEntries().length} total • ${entries.filter((entry) => entry.tracked).length} tracked in view`;
  els.selectionSummary.textContent = state.selectedId
    ? (() => {
      const selected = getEntryById(state.selectedId);
      return selected
        ? `Selected ${selected.rof2_opcode || "—"} • ${selected.rof2_name || selected.eqemu_name || "(unnamed)"}`
        : "No opcode selected.";
    })()
    : "No opcode selected.";
  els.pageSummary.textContent = entries.length
    ? `Page ${page} of ${totalPages} • showing ${startIndex + 1}-${endIndex}`
    : "Page 1 of 1 • showing 0-0";
  els.prevPageButton.disabled = page <= 1;
  els.nextPageButton.disabled = page >= totalPages;

  for (const entry of pageEntries) {
    const fragment = els.rowTemplate.content.cloneNode(true);
    const row = fragment.querySelector("tr");
    row.dataset.entryId = entry.id;
    if (entry.id === state.selectedId) row.classList.add("selected");
    if (entry.alertEnabled) row.classList.add("alert-armed");
    if (state.recentDetectedEntryIds.has(entry.id)) row.classList.add("recent-detection");
    if (state.recentCountEntryIds.has(entry.id)) row.classList.add("recent-count-pulse");

    const alertCell = fragment.querySelector(".alert-cell");
    const alertButton = document.createElement("button");
    alertButton.type = "button";
    alertButton.className = `alert-toggle${entry.alertEnabled ? " enabled" : ""}`;
    alertButton.textContent = entry.alertEnabled ? "On" : "Off";
    alertButton.title = entry.alertEnabled ? "Alert armed for live detection" : "Enable live detection alert";
    alertButton.addEventListener("click", (event) => {
      event.stopPropagation();
      state.selectedId = entry.id;
      updateSelectedEntry({ alertEnabled: !entry.alertEnabled });
    });
    alertCell.appendChild(alertButton);

    fragment.querySelector(".opcode-cell").textContent = entry.rof2_opcode || "—";
    fragment.querySelector(".count-cell").textContent = String(entryCounts.get(entry.id) || 0);
    fragment.querySelector(".name-cell").textContent = entry.rof2_name || entry.eqemu_name || "(unnamed)";
    fragment.querySelector(".eqemu-cell").textContent = entry.eqemu_name || "—";
    const statusPill = fragment.querySelector(".status-pill");
    statusPill.textContent = getStatusLabel(entry.status);
    statusPill.dataset.status = entry.status;
    const sourcePill = fragment.querySelector(".source-pill");
    sourcePill.textContent = getSourceLabel(entry.source_type);
    sourcePill.dataset.source = entry.source_type;
    fragment.querySelector(".notes-cell").textContent = getEntryNoteText(entry) || "—";

    row.addEventListener("click", () => {
      state.selectedId = entry.id;
      setInspectorModal(true);
      renderTable();
      renderBookmarks();
      renderInspector();
    });

    els.opcodeTableBody.appendChild(fragment);
  }
}

function renderLiveSession() {
  const live = state.liveSession || { status: "idle", markers: [] };
  const markerCount = live.markerCount ?? live.markers?.length ?? 0;
  const isRunning = live.status === "running";
  const hasActivity = (live.activityCount || live.activity?.length || 0) > 0;
  const desiredInterface = isRunning
    ? (live.interface || live.resolvedInterface || "loopback")
    : (live.interface || getCurrentInterfaceSelection() || getPreferredCaptureInterfaceValue());
  els.sessionStatusBadge.textContent = live.status || "idle";
  els.sessionStatusBadge.dataset.status = live.status || "idle";
  els.sessionNameValue.textContent = live.sessionName || "No session loaded";
  els.sessionMarkersValue.textContent = String(markerCount);
  els.sessionInterfaceValue.textContent = live.interface || live.resolvedInterface || "—";
  els.sessionFilterValue.textContent = live.captureFilter || "—";
  els.sessionDetectionsValue.textContent = String(live.detectionCount || 0);
  if (!state.sessionFormInitialized) {
    if (isRunning && live.sessionName) {
      els.sessionNameInput.value = live.sessionName;
      state.sessionNameSeed = "";
    } else {
      forceSeedNextSessionName(live.sessionName || els.sessionNameInput.value || "rof2-live-ui");
    }
    els.sessionCaptureFilterInput.value = live.captureFilter || els.sessionCaptureFilterInput.value || "udp";
    state.sessionFormInitialized = true;
  } else if (!isRunning) {
    maybeSeedNextSessionName(live.sessionName || "rof2-live-ui");
  }
  renderInterfaceOptions(desiredInterface);
  els.sessionToggleButton.disabled = state.sessionActionPending;
  els.sessionToggleButton.textContent = isRunning ? "Stop Capture" : "Start Capture";
  els.sessionToggleButton.classList.toggle("button-primary", !isRunning);
  els.sessionToggleButton.classList.toggle("button-danger", isRunning);
  els.restartSessionButton.hidden = !isRunning;
  els.restartSessionButton.disabled = state.sessionActionPending || !isRunning;
  els.openLiveMonitorButton.disabled = state.sessionActionPending || (!isRunning && !hasActivity);
  els.markSessionButton.disabled = state.sessionActionPending || !live.sessionName || !isRunning;
  els.sessionNameInput.disabled = state.sessionActionPending;
  els.sessionInterfaceInput.disabled = state.sessionActionPending || isRunning;
  els.sessionCaptureFilterInput.disabled = state.sessionActionPending || isRunning;
  els.sessionDurationInput.disabled = state.sessionActionPending || isRunning;
  els.sessionMarkerLabelInput.disabled = state.sessionActionPending || !isRunning;
  els.sessionMarkerNoteInput.disabled = state.sessionActionPending || !isRunning;
  els.sessionTimingValue.textContent = live.startedUtc
    ? `Started ${formatCompactTimestamp(live.startedUtc)}${live.stoppedUtc ? `  •  Stopped ${formatCompactTimestamp(live.stoppedUtc)}` : ""}`
    : "No live session state yet.";
  if (live.capturePath) {
    const lines = [live.capturePath];
    if (live.markersPath) lines.push(live.markersPath);
    els.sessionPathsValue.textContent = lines.join("\n");
  } else {
    els.sessionPathsValue.textContent = "Use the skill to start a session and OCC will follow it here.";
  }

  renderLiveMonitor();
}

function renderLiveMonitor() {
  if (!state.liveMonitorOpen && els.liveMonitorModal.hidden) {
    return;
  }
  const live = state.liveSession || { status: "idle", activity: [] };
  const sessionKey = getSessionKey(live);
  if (state.liveMonitorFeedBaseline.sessionKey && state.liveMonitorFeedBaseline.sessionKey !== sessionKey) {
    state.liveMonitorFeedBaseline = { sessionKey: "", frameNumber: 0, timeEpoch: 0 };
  }
  const allActivity = getActivityEntries();
  const baselineActivity = allActivity.filter((item) => activityIsAfterBaseline(item));
  const suppressedItems = getSuppressedActivityEntries(baselineActivity);
  const visibleActivity = baselineActivity.filter((item) => !item.suppressed);
  const filtered = sortLiveMonitorItems(visibleActivity.filter(activityMatchesFilters));
  const groupedFeedItems = groupLiveMonitorItems(filtered);
  const countLimit = getLiveMonitorCountLimit();
  const limitedGroupedFeedItems = countLimit
    ? groupedFeedItems.filter((item) => item.feedCount <= countLimit)
    : groupedFeedItems;
  const visibleItems = limitedGroupedFeedItems.slice(0, LIVE_MONITOR_RENDER_LIMIT);
  const visibleSuppressedRules = getSuppressionRules();
  const feedPacketCount = limitedGroupedFeedItems.reduce((sum, item) => sum + item.feedCount, 0);
  const candidateCount = limitedGroupedFeedItems.reduce((sum, item) => sum + (item.hasCandidates ? item.feedCount : 0), 0);
  const flaggedCount = limitedGroupedFeedItems.reduce((sum, item) => sum + (item.hasFlaggedMatches ? item.feedCount : 0), 0);
  const eqLikeCount = limitedGroupedFeedItems.reduce((sum, item) => sum + (item.eqLike ? item.feedCount : 0), 0);
  const highConfidenceCount = limitedGroupedFeedItems.reduce((sum, item) => sum + (item.eqConfidence === "high" ? item.feedCount : 0), 0);
  const ignoredCount = limitedGroupedFeedItems.reduce((sum, item) => sum + (item.ignored ? item.feedCount : 0), 0);
  const knownOnlyCount = visibleActivity.filter((item) => item.onlyKnownOpcode).length;
  const hiddenByCountLimit = groupedFeedItems.reduce((sum, item) => sum + (countLimit && item.feedCount > countLimit ? item.feedCount : 0), 0);
  const hiddenFeedEntriesByCountLimit = groupedFeedItems.length - limitedGroupedFeedItems.length;
  const onlyNoise = feedPacketCount > 0 && ignoredCount === feedPacketCount && candidateCount === 0;
  const totalActivityCount = baselineActivity.length;
  const hasFeedBaseline = Boolean(state.liveMonitorFeedBaseline.sessionKey === sessionKey && (state.liveMonitorFeedBaseline.frameNumber || state.liveMonitorFeedBaseline.timeEpoch));
  const summaryParts = [];
  const showingSuppressed = state.liveMonitorFilters.tab === "suppressed";

  els.liveFeedTabButton.classList.toggle("active", !showingSuppressed);
  els.liveFeedTabButton.setAttribute("aria-selected", String(!showingSuppressed));
  els.suppressedTabButton.classList.toggle("active", showingSuppressed);
  els.suppressedTabButton.setAttribute("aria-selected", String(showingSuppressed));
  els.clearLiveMonitorFeedButton.hidden = showingSuppressed;
  els.clearLiveMonitorFeedButton.disabled = !live.sessionName || (!allActivity.length && !hasFeedBaseline);
  els.liveMonitorModeFilter.closest(".field").hidden = showingSuppressed;
  els.liveMonitorCountLimitInput.closest(".field").hidden = showingSuppressed;
  els.liveMonitorUnknownOnlyToggle.closest(".field").hidden = showingSuppressed;
  els.liveMonitorCountLimitInput.value = state.liveMonitorFilters.countLimit;
  els.liveMonitorUnknownOnlyToggle.checked = state.liveMonitorFilters.unknownOnly;

  els.liveMonitorEyebrow.textContent = live.sessionName
    ? `${live.status === "running" ? "Monitoring" : "Last capture"} • ${live.sessionName}`
    : "Live monitor";
  if (showingSuppressed) {
    summaryParts.push(`${visibleSuppressedRules.length} suppression rule${visibleSuppressedRules.length === 1 ? "" : "s"}`);
    if (suppressedItems.length) {
      summaryParts.push(`${suppressedItems.length} currently hidden packet${suppressedItems.length === 1 ? "" : "s"}`);
    }
  } else if (live.status === "running") {
    summaryParts.push(`${feedPacketCount} visible packet${feedPacketCount === 1 ? "" : "s"}`);
  } else if (baselineActivity.length) {
    summaryParts.push(`${feedPacketCount} packet${feedPacketCount === 1 ? "" : "s"} from the latest session`);
  } else {
    summaryParts.push("Watch live UDP activity, candidate opcodes, and flagged matches during an active capture.");
  }
  if (live.interface || live.resolvedInterface) {
    summaryParts.push(`interface ${live.interface || live.resolvedInterface}`);
  }
  if (live.captureFilter) {
    summaryParts.push(`filter ${live.captureFilter}`);
  }
  if (!showingSuppressed && state.liveMonitorFilters.mode === "eqlike") {
    summaryParts.push(`EQ-like lane${highConfidenceCount ? ` • ${highConfidenceCount} high-confidence` : ""}`);
  }
  if (!showingSuppressed && limitedGroupedFeedItems.length) {
    summaryParts.push(`${limitedGroupedFeedItems.length} feed entr${limitedGroupedFeedItems.length === 1 ? "y" : "ies"}`);
    if (countLimit) {
      summaryParts.push(`count limit ≤ ${countLimit}`);
      if (hiddenFeedEntriesByCountLimit > 0) {
        summaryParts.push(`${hiddenFeedEntriesByCountLimit} entr${hiddenFeedEntriesByCountLimit === 1 ? "y" : "ies"} hidden`);
      }
    }
    if (limitedGroupedFeedItems.length > visibleItems.length) {
      summaryParts.push(`showing newest ${visibleItems.length}`);
    }
  }
  if (!showingSuppressed && hasFeedBaseline) {
    summaryParts.push("feed cleared");
  }
  if (!showingSuppressed && state.liveMonitorFilters.unknownOnly) {
    summaryParts.push(`unknown only${knownOnlyCount ? ` • ${knownOnlyCount} known packet${knownOnlyCount === 1 ? "" : "s"} hidden` : ""}`);
  }
  if (!showingSuppressed && suppressedItems.length) {
    summaryParts.push(`${suppressedItems.length} packet${suppressedItems.length === 1 ? "" : "s"} hidden by suppression`);
  }
  if (!showingSuppressed && live.status === "running" && totalActivityCount > 0 && feedPacketCount === 0) {
    if (visibleActivity.length === 0 && suppressedItems.length > 0) {
      summaryParts.push("all visible packets are currently suppressed");
    } else if (state.liveMonitorFilters.mode === "eqlike") {
      summaryParts.push(`${totalActivityCount} total packet${totalActivityCount === 1 ? "" : "s"} seen, but none classified as EQ-like`);
    } else if (state.liveMonitorFilters.mode === "candidates") {
      summaryParts.push(`${totalActivityCount} total packet${totalActivityCount === 1 ? "" : "s"} seen, but none produced opcode candidates`);
    } else if (state.liveMonitorFilters.mode === "flagged") {
      summaryParts.push(`${totalActivityCount} total packet${totalActivityCount === 1 ? "" : "s"} seen, but none matched flagged alerts`);
    }
    if (countLimit && hiddenByCountLimit > 0) {
      summaryParts.push(`${hiddenByCountLimit} packet${hiddenByCountLimit === 1 ? "" : "s"} hidden by Count Limit`);
    }
  }
  if (!showingSuppressed && onlyNoise) {
    summaryParts.push("only known-noise traffic seen; likely wrong interface");
  }
  els.liveMonitorSummary.textContent = summaryParts.join(" • ");

  els.liveMonitorPacketsValue.textContent = String(showingSuppressed ? suppressedItems.length : feedPacketCount);
  els.liveMonitorCandidatesValue.textContent = String(showingSuppressed ? visibleSuppressedRules.filter((rule) => rule.type === "opcode").length : candidateCount);
  els.liveMonitorFlaggedValue.textContent = String(showingSuppressed ? suppressedItems.filter((item) => item.hasFlaggedMatches).length : flaggedCount);
  els.liveMonitorPacketsValue.title = !showingSuppressed && state.liveMonitorFilters.mode === "eqlike"
    ? `${eqLikeCount} EQ-like packets currently visible`
    : "";
  els.liveMonitorSyncValue.textContent = live.syncedUtc
    ? formatCompactTimestamp(live.syncedUtc)
    : live.status === "running"
      ? "Waiting for first sync"
      : "No live sync";

  const liveEmptyMessage = live.status === "running"
    ? state.liveMonitorFilters.mode === "eqlike"
      ? totalActivityCount > 0
        ? state.liveMonitorFilters.unknownOnly
          ? "Capture is running and seeing UDP traffic, but the current view is hiding already-known opcode mappings. Turn off Unknown only or switch View to All traffic to inspect more packets."
          : countLimit && hiddenByCountLimit > 0 && feedPacketCount === 0
            ? "Capture is running and seeing EQ-like traffic, but every grouped feed entry is above the current Count Limit. Raise or clear the limit to bring those packets back."
          : "Capture is running and seeing UDP traffic, but none of it currently qualifies as EQ-like. Switch View to All traffic to inspect raw packets, or restart on the correct interface."
        : hasFeedBaseline
          ? "Feed cleared. Waiting for new traffic that looks like EverQuest session traffic."
          : "Capture is running. Waiting for traffic that looks like EverQuest session traffic."
      : state.liveMonitorFilters.mode === "candidates"
        ? totalActivityCount > 0
          ? state.liveMonitorFilters.unknownOnly
            ? "Capture is running and seeing opcode traffic, but the current view is hiding already-known opcode mappings. Turn off Unknown only to see the full candidate lane."
            : countLimit && hiddenByCountLimit > 0 && feedPacketCount === 0
              ? "Capture is running and seeing opcode traffic, but every grouped feed entry is above the current Count Limit. Raise or clear the limit to inspect those candidates."
            : "Capture is running and seeing UDP traffic, but nothing currently resolves to opcode candidates. Switch View to All traffic to inspect raw packets."
          : hasFeedBaseline
            ? "Feed cleared. Waiting for new opcode traffic that passes the current monitor filters."
            : "Capture is running. Waiting for UDP traffic that passes the current monitor filters."
        : state.liveMonitorFilters.mode === "flagged"
          ? totalActivityCount > 0
            ? "Capture is running and seeing UDP traffic, but none of it matches your flagged alerts yet."
            : hasFeedBaseline
              ? "Feed cleared. Waiting for new traffic that matches your flagged alerts."
              : "Capture is running. Waiting for UDP traffic that passes the current monitor filters."
      : hasFeedBaseline
        ? "Feed cleared. Waiting for new UDP traffic that passes the current monitor filters."
        : "Capture is running. Waiting for UDP traffic that passes the current monitor filters."
    : baselineActivity.length
      ? visibleActivity.length === 0 && suppressedItems.length > 0
        ? "All packets in the current live activity window are hidden by suppression rules. Use the Suppressed tab to review or remove them."
        : "No packets match the current live monitor filters."
      : "Start a capture session to watch live packet activity here.";
  const suppressedEmptyMessage = "No suppression rules yet.";

  els.liveMonitorEmpty.textContent = showingSuppressed ? suppressedEmptyMessage : liveEmptyMessage;
  els.liveMonitorEmpty.hidden = showingSuppressed || visibleItems.length > 0;
  els.liveMonitorList.hidden = showingSuppressed || visibleItems.length === 0;
  els.suppressedListEmpty.hidden = !showingSuppressed || visibleSuppressedRules.length > 0;
  els.suppressedList.hidden = !showingSuppressed || visibleSuppressedRules.length === 0;

  if (!showingSuppressed && !visibleItems.length) {
    els.liveMonitorList.innerHTML = "";
    return;
  }

  if (!showingSuppressed) {
    els.liveMonitorList.innerHTML = visibleItems.map((item) => {
    const isRecent = Boolean(item.recentInGroup);
    const primaryNames = item.displayName || getDetectionDisplayName(item);
    const secondaryNames = item.opcodeAlias
      ? getDefaultDetectionLabel(item)
      : "";
    const info = normalizeMonitorText(item.info || "No tshark info");
    const payload = item.payloadPrefix || "No payload prefix captured";
    const badges = [
      item.feedCount > 1 ? `<span class="badge monitor-badge-count">${escapeHtml(`${item.feedCount}x in feed`)}</span>` : "",
      `<span class="badge">${escapeHtml(`Frame ${item.frameNumber || "?"}`)}</span>`,
      item.opcode ? `<span class="badge monitor-badge-candidate">${escapeHtml(item.opcode)}</span>` : "",
      item.opcodeAlias ? `<span class="badge monitor-badge-alias">Custom label</span>` : "",
      item.eqLike && !item.hasFlaggedMatches ? `<span class="badge monitor-badge-eqlike">${escapeHtml(item.eqLikeReason || "EQ-like")}</span>` : "",
      item.eqConfidence && item.eqConfidence !== "none" ? `<span class="badge monitor-badge-confidence-${escapeHtml(item.eqConfidence)}">${escapeHtml(`${item.eqConfidence} confidence`)}</span>` : "",
      item.analysisSource ? `<span class="badge">${escapeHtml(item.analysisSource)}</span>` : "",
      item.hasFlaggedMatches ? `<span class="badge monitor-badge-flagged">Flagged Match</span>` : "",
      item.ignored ? `<span class="badge monitor-badge-noise">${escapeHtml(item.ignoredReason || "Known noise")}</span>` : "",
      !item.opcode && item.hasCandidates ? `<span class="badge monitor-badge-candidate">Candidates</span>` : "",
    ].filter(Boolean).join("");
    const matchedDetail = item.matchedEntries.length
      ? `<p class="monitor-item-match">Matched alert rows: ${escapeHtml(item.matchedEntries.map((entry) => `${entry.rof2_opcode || "—"} ${entry.rof2_name || entry.eqemu_name || ""}`.trim()).join(" • "))}</p>`
      : "";
    const candidateDetail = !item.matchedEntries.length && item.candidates?.length
      ? `<p class="monitor-item-match">Candidate names: ${escapeHtml(item.candidates.map((candidate) => {
        const label = candidate.name || candidate.names?.join(", ") || "";
        return `${candidate.opcode || "—"} ${label}`.trim();
      }).join(" • "))}</p>`
      : "";
    const titleAttr = item.matchedEntries[0]?.id ? ` data-entry-id="${escapeHtml(item.matchedEntries[0].id)}"` : "";
    const clickableClass = item.matchedEntries[0]?.id ? " clickable" : "";
    const opcodeAttr = item.opcode ? ` data-opcode="${escapeHtml(item.opcode)}"` : "";
    const detectionAttr = item.id ? ` data-detection-id="${escapeHtml(item.id)}"` : "";
    const secondaryDetail = secondaryNames && secondaryNames !== primaryNames
      ? `<p class="monitor-item-alias">Default label: ${escapeHtml(secondaryNames)}</p>`
      : "";
    const copyButton = item.payloadPrefix
      ? `<button class="monitor-item-copy-button monitor-item-menu-button" type="button" aria-label="Copy packet data" title="Copy packet data" data-copy-payload="${escapeHtml(item.payloadPrefix)}">
          <svg viewBox="0 0 24 24" aria-hidden="true" focusable="false"><path d="M16 1H6a2 2 0 0 0-2 2v12h2V3h10V1Zm3 4H10a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h9a2 2 0 0 0 2-2V7a2 2 0 0 0-2-2Zm0 16H10V7h9v14Z"/></svg>
        </button>`
      : "";
    const menuButton = item.id
      ? `<button class="monitor-item-menu-button" type="button" aria-label="Open packet actions" title="Packet actions" data-detection-id="${escapeHtml(item.id)}" data-group-key="${escapeHtml(item.groupKey || "")}">⋯</button>`
      : "";

    return `
      <article class="monitor-item${clickableClass}${isRecent ? " monitor-item-new" : ""}" data-level="${item.hasFlaggedMatches ? "flagged" : item.eqConfidence === "low" ? "uncertain" : item.hasCandidates ? "candidate" : item.eqLike ? "eq-like" : item.ignored ? "noise" : "neutral"}"${titleAttr}${opcodeAttr}${detectionAttr} data-group-key="${escapeHtml(item.groupKey || "")}">
        <div class="monitor-item-header">
          <div class="monitor-badges">${badges}</div>
          <div class="monitor-item-header-actions">
            <span class="results-summary">${escapeHtml(formatCompactTimestamp(item.detectedUtc))}</span>
            ${copyButton}
            ${menuButton}
          </div>
        </div>
        <h3 class="monitor-item-title">${escapeHtml(primaryNames)}</h3>
        ${secondaryDetail}
        <p class="monitor-item-route">${escapeHtml(`${item.src || "?"}:${item.srcport || "?"} -> ${item.dst || "?"}:${item.dstport || "?"}`)} • ${escapeHtml(`${item.length || "?"} bytes`)}</p>
        <p class="monitor-item-info">${escapeHtml(info)}</p>
        <p class="monitor-item-payload">${escapeHtml(payload)}</p>
        ${matchedDetail || candidateDetail}
      </article>
    `;
    }).join("");
    return;
  }

  const activityByRule = new Map();
  for (const item of suppressedItems) {
    const ruleKey = item.suppressionRule?.key;
    if (!ruleKey) continue;
    if (!activityByRule.has(ruleKey)) activityByRule.set(ruleKey, []);
    activityByRule.get(ruleKey).push(item);
  }

  els.suppressedList.innerHTML = visibleSuppressedRules.map((rule) => {
    const matchingItems = activityByRule.get(rule.key) || [];
    const latestItem = matchingItems[0];
    const sampleRoute = latestItem
      ? `${latestItem.src || "?"}:${latestItem.srcport || "?"} -> ${latestItem.dst || "?"}:${latestItem.dstport || "?"}`
      : rule.sampleRoute || rule.matcherLabel || rule.signature || rule.key;
    const sampleLength = latestItem?.length || rule.sampleLength || "?";
    const sampleInfo = normalizeMonitorText(latestItem?.info || rule.sampleInfo || "No tshark info");
    const samplePayload = latestItem?.payloadPrefix || rule.samplePayloadPrefix || "No payload prefix captured";
    const sampleDetectedUtc = latestItem?.detectedUtc || rule.sampleDetectedUtc || rule.createdAt;
    const badges = [
      `<span class="badge">${escapeHtml(rule.type === "opcode" ? "Opcode rule" : "Packet rule")}</span>`,
      rule.opcode ? `<span class="badge monitor-badge-candidate">${escapeHtml(rule.opcode)}</span>` : "",
      rule.sampleAnalysisSource ? `<span class="badge">${escapeHtml(rule.sampleAnalysisSource)}</span>` : "",
      matchingItems.length ? `<span class="badge monitor-badge-noise">${escapeHtml(`${matchingItems.length} hidden`)}</span>` : "",
    ].filter(Boolean).join("");
    return `
      <article class="monitor-item suppressed-rule-card" data-rule-key="${escapeHtml(rule.key)}" data-level="${rule.type === "opcode" ? "candidate" : "noise"}">
        <div class="monitor-item-header">
          <div class="monitor-badges">${badges}</div>
          <span class="results-summary">${escapeHtml(formatCompactTimestamp(rule.createdAt))}</span>
        </div>
        <h3 class="monitor-item-title">${escapeHtml(rule.label || rule.matcherLabel || rule.key)}</h3>
        <p class="monitor-item-route">${escapeHtml(sampleRoute)} • ${escapeHtml(`${sampleLength} bytes`)}</p>
        <p class="monitor-item-info">${escapeHtml(latestItem ? `Latest hidden packet: frame ${latestItem.frameNumber} • ${formatCompactTimestamp(sampleDetectedUtc)}` : `Suppressed packet snapshot: frame ${rule.sampleFrameNumber || "?"} • ${formatCompactTimestamp(sampleDetectedUtc)}`)}</p>
        <p class="monitor-item-info">${escapeHtml(sampleInfo)}</p>
        <p class="monitor-item-payload">${escapeHtml(samplePayload)}</p>
        <div class="inline-actions monitor-rule-actions">
          <button class="button button-quiet unsuppress-rule-button" type="button" data-rule-key="${escapeHtml(rule.key)}">Unsuppress</button>
        </div>
      </article>
    `;
  }).join("");
}

function renderBookmarks() {
  const bookmarks = getBookmarks();
  els.bookmarkEmpty.hidden = bookmarks.length > 0;
  els.bookmarkWorkspace.hidden = bookmarks.length === 0;
  els.bookmarkList.innerHTML = "";
  if (!bookmarks.length) {
    state.selectedBookmarkId = null;
    state.bookmarkPagination.page = 1;
    setBookmarkModal(false);
    els.bookmarkPageSummary.textContent = "Page 1 of 1 • showing 0-0";
    els.prevBookmarkPageButton.disabled = true;
    els.nextBookmarkPageButton.disabled = true;
    return;
  }

  if (!getBookmarkById(state.selectedBookmarkId)) {
    state.selectedBookmarkId = bookmarks[0]?.id || null;
  }

  const { page, totalPages, startIndex, endIndex, pageBookmarks } = getBookmarkPaginationMeta(bookmarks);
  state.bookmarkPagination.page = page;
  els.bookmarkPageSummary.textContent = `Page ${page} of ${totalPages} • showing ${startIndex + 1}-${endIndex}`;
  els.prevBookmarkPageButton.disabled = page <= 1;
  els.nextBookmarkPageButton.disabled = page >= totalPages;

  for (const bookmark of pageBookmarks) {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "bookmark-item";
    if (bookmark.id === state.selectedBookmarkId) button.classList.add("selected");
    const noteText = bookmark.Note ? `<span class="bookmark-note">${bookmark.Note}</span>` : "";
    button.innerHTML = `
      <span class="bookmark-label">${bookmark.Label || "bookmark"}</span>
      <span class="bookmark-meta">${formatCompactTimestamp(bookmark.MarkedUtc)}</span>
      ${noteText}
    `;
    button.addEventListener("click", () => {
      state.selectedBookmarkId = bookmark.id;
      setBookmarkModal(true);
      renderBookmarks();
      renderBookmarkModal();
    });
    els.bookmarkList.appendChild(button);
  }
}

function renderBookmarkModal() {
  const active = getBookmarkById(state.selectedBookmarkId);
  if (!active) {
    setBookmarkModal(false);
    return;
  }

  els.bookmarkTitle.textContent = active.Label || "Marker";
  els.bookmarkTime.textContent = formatCompactTimestamp(active.MarkedUtc);
  els.bookmarkSourceNote.textContent = active.Note || "No marker note captured.";
  els.bookmarkExpectedOpcode.value = active.expectedOpcode;
  const linkedEntry = active.linkedEntryId ? getEntryById(active.linkedEntryId) : null;
  els.bookmarkLinkedEntry.textContent = linkedEntry
    ? `${linkedEntry.rof2_opcode || "—"} • ${linkedEntry.rof2_name || linkedEntry.eqemu_name || "(unnamed)"}`
    : "No linked opcode row.";
  els.bookmarkNotes.value = active.notes;
  els.linkSelectedOpcodeButton.disabled = !state.selectedId;
  setBookmarkModal(state.bookmarkModalOpen);
}

function renderInspector() {
  const entry = getEntryById(state.selectedId);
  if (!entry) {
    setInspectorModal(false);
    return;
  }

  els.inspectorContent.hidden = false;
  els.inspectorTitle.textContent = entry.rof2_name || entry.eqemu_name || entry.rof2_opcode || "Custom Entry";
  els.inspectorSourceLine.textContent = entry.source_type === "custom" ? "Custom entry" : getSourceLabel(entry.source_type);
  els.inspectorAlignment.textContent = getAlignmentLabel(entry.alignment);
  els.inspectorTrackedState.textContent = entry.tracked ? "Tracked locally" : "Seed entry";
  els.inspectorLastTouched.textContent = formatTimestamp(entry.updatedAt);
  const customEditable = entry.source_type === "custom";
  els.inspectorOpcode.value = entry.rof2_opcode || "";
  els.inspectorSheetName.value = entry.rof2_name || "";
  els.inspectorEqemuName.value = entry.eqemu_name || "";
  els.inspectorTestOpcode.value = entry.test_opcode || "";
  els.inspectorOpcode.readOnly = !customEditable;
  els.inspectorSheetName.readOnly = !customEditable;
  els.inspectorEqemuName.readOnly = !customEditable;
  els.inspectorTestOpcode.readOnly = !customEditable;
  els.inspectorOpcode.placeholder = customEditable ? "0x1234" : "—";
  els.inspectorSheetName.placeholder = customEditable ? "Candidate name" : "—";
  els.inspectorEqemuName.placeholder = customEditable ? "OP_CustomCandidate" : "—";
  els.inspectorTestOpcode.placeholder = customEditable ? "0x1234" : "—";
  els.entryStatus.value = entry.status;
  els.entryConfidence.value = entry.confidence;
  els.entryAlertEnabled.checked = entry.alertEnabled;
  els.entryTags.value = entry.tags;
  els.entryNotes.value = entry.userNotes;
  const stageIndex = getWorkflowStageIndex(entry.workflowStage);
  els.inspectorWorkflowStageSummary.textContent = WORKFLOW_STAGE_LABELS[entry.workflowStage] || entry.workflowStage;
  els.inspectorWorkflowProgressPill.textContent = `Step ${stageIndex + 1} of ${WORKFLOW_STAGES.length}`;
  if (state.workflowModalOpen) renderWorkflowModal();
  els.entryReferenceNotes.value = [
    entry.packet_signature ? `Packet Signature: ${entry.packet_signature}` : "",
    entry.packet_family_key ? `Packet Family: ${entry.packet_family_key}` : "",
    entry.notes,
    entry.extra,
  ].filter(Boolean).join("\n\n") || "No reference notes.";
  els.deleteCustomEntryButton.hidden = entry.source_type !== "custom";

  const entries = getVisibleEntries();
  const currentIndex = entries.findIndex((e) => e.id === state.selectedId);
  els.prevEntryButton.disabled = currentIndex <= 0;
  els.nextEntryButton.disabled = currentIndex === -1 || currentIndex >= entries.length - 1;

  setInspectorModal(state.inspectorOpen || Boolean(state.selectedId));
}

function renderWorkflowModal() {
  const entry = getEntryById(state.selectedId);
  if (!entry) {
    setWorkflowModal(false);
    return;
  }

  const stageIndex = getWorkflowStageIndex(entry.workflowStage);
  const checklist = getWorkflowChecklist(entry);
  const analysis = buildWorkflowAnalysis(entry);
  const sessionCount = analysis.evidence.sessionCount;
  const markerCount = analysis.evidence.markerCount;

  els.workflowModalTitle.textContent = WORKFLOW_STAGE_LABELS[entry.workflowStage] || entry.workflowStage;
  els.workflowEntryOpcode.textContent = entry.rof2_opcode || entry.test_opcode || "—";
  els.workflowEntryName.textContent = entry.rof2_name || entry.eqemu_name || "Custom Entry";
  els.entryWorkflowStage.value = entry.workflowStage;
  els.workflowProgressPill.textContent = `Step ${stageIndex + 1} of ${WORKFLOW_STAGES.length}`;
  els.workflowSessionCount.textContent = String(sessionCount);
  els.workflowMarkerCount.textContent = String(markerCount);
  els.workflowIdentityValue.textContent = getWorkflowIdentityLabel(entry);
  els.workflowAlertValue.textContent = entry.alertEnabled ? "Armed" : "Off";
  els.workflowRepeatableCheck.checked = checklist.repeatable;
  els.workflowIsolatedCheck.checked = checklist.isolated;
  els.workflowHypothesisCheck.checked = checklist.hypothesis;
  els.workflowConfirmedCheck.checked = checklist.confirmed;
  els.workflowAnalysisSummary.value = analysis.summary;
  els.workflowNextStep.value = getWorkflowNextStep(entry, { sessionCount, markerCount }, analysis);
  els.workflowReferenceHints.value = buildWorkflowReferenceHints(entry, analysis);
  els.workflowBackButton.disabled = stageIndex <= 0;
  els.workflowAdvanceButton.disabled = stageIndex >= WORKFLOW_STAGES.length - 1;
  els.workflowSyncStageButton.textContent = `Use Suggested Stage: ${WORKFLOW_STAGE_LABELS[analysis.suggestedStage] || analysis.suggestedStage}`;
  els.workflowSyncStageButton.disabled = analysis.suggestedStage === entry.workflowStage;
  els.workflowApplyHypothesisButton.disabled = !analysis.bestCandidate;
  els.workflowApplyHypothesisButton.textContent = analysis.bestCandidate
    ? `Record ${analysis.bestCandidate.opcode}`
    : "Record Suggested Hypothesis";
  els.workflowPromoteHypothesisButton.disabled = !entry.test_opcode || entry.rof2_opcode === entry.test_opcode;

  const steps = els.workflowStepper.querySelectorAll(".workflow-step");
  const connectors = els.workflowStepper.querySelectorAll(".workflow-step-connector");
  steps.forEach((stepEl) => {
    const stage = stepEl.dataset.stage;
    const idx = getWorkflowStageIndex(stage);
    stepEl.classList.toggle("is-completed", idx < stageIndex);
    stepEl.classList.toggle("is-active", idx === stageIndex);
  });
  connectors.forEach((conn, i) => {
    conn.classList.toggle("is-filled", i < stageIndex);
  });
}

function renderDetectionAlertModal() {
  if (!state.currentAlertMatches.length) {
    els.detectionAlertList.innerHTML = "";
    setDetectionAlertModal(false);
    return;
  }

  const totalAlerts = state.currentAlertMatches.reduce((count, match) => count + Math.max(match.entries.length, match.labels?.length || 0, 1), 0);
  els.detectionAlertSummary.textContent = `${state.currentAlertMatches.length} detection${state.currentAlertMatches.length === 1 ? "" : "s"} matched ${totalAlerts} flagged opcode alert${totalAlerts === 1 ? "" : "s"}.`;
  els.detectionAlertList.innerHTML = state.currentAlertMatches.map((match) => `
    <article class="alert-card">
      <div class="alert-card-header">
        <span class="badge">${match.detection.opcode || "Detected"}</span>
        <span class="results-summary">${formatCompactTimestamp(match.detection.detectedUtc)} • frame ${match.detection.frameNumber}</span>
      </div>
      <p class="alert-card-title">${(match.labels || []).join(", ") || match.detection.opcode || "Detected opcode"}</p>
      <p class="results-summary">${match.detection.src || "?"}:${match.detection.srcport || "?"} -> ${match.detection.dst || "?"}:${match.detection.dstport || "?"}</p>
    </article>
  `).join("");
  setDetectionAlertModal(true);
}

function dismissDetectionAlerts() {
  state.currentAlertMatches = [];
  renderDetectionAlertModal();
}

function openFirstDetectionAlertEntry() {
  const firstMatch = state.currentAlertMatches[0];
  const firstEntry = firstMatch?.entries?.[0] || getEntriesForOpcode(firstMatch?.detection?.opcode)[0];
  if (!firstEntry) return;
  state.selectedId = firstEntry.id;
  dismissDetectionAlerts();
  setInspectorModal(true);
  renderTable();
  renderBookmarks();
  renderInspector();
}

async function processLiveDetections() {
  const sessionKey = getSessionKey(state.liveSession);
  const detections = state.liveSession.detections || [];
  const activity = state.liveSession.activity || [];

  if (sessionKey !== state.currentSessionKey) {
    state.currentSessionKey = sessionKey;
    state.liveMonitorFeedBaseline = { sessionKey: "", frameNumber: 0, timeEpoch: 0 };
    state.seenDetectionIds = new Set(detections.map((detection) => detection.id));
    state.seenActivityIds = new Set(activity.map((item) => item.id).filter(Boolean));
    clearRecentActivityTimers();
    clearRecentCountTimers();
    state.recentDetectedEntryIds = new Set();
    state.currentAlertMatches = [];
    state.countPulseSessionKey = sessionKey;
    state.lastSessionEntryCounts = new Map(getSessionEntryCounts());
    state.detectionBootstrapComplete = true;
    renderDetectionAlertModal();
    renderTable();
    return;
  }

  if (!state.detectionBootstrapComplete) {
    state.seenDetectionIds = new Set(detections.map((detection) => detection.id));
    state.seenActivityIds = new Set(activity.map((item) => item.id).filter(Boolean));
    state.countPulseSessionKey = sessionKey;
    state.lastSessionEntryCounts = new Map(getSessionEntryCounts());
    state.detectionBootstrapComplete = true;
    return;
  }

  const newDetections = detections.filter((detection) => !state.seenDetectionIds.has(detection.id));
  for (const detection of newDetections) {
    state.seenDetectionIds.add(detection.id);
  }

  const newActivity = activity.filter((item) => item?.id && !state.seenActivityIds.has(item.id));
  for (const item of newActivity) {
    state.seenActivityIds.add(item.id);
  }
  if (newActivity.length) {
    markActivityItemsRecent(newActivity.map((item) => item.id));
  }

  const changedEntryIds = syncSessionOpcodeCountPulse();

  const alertCandidates = new Map();
  for (const item of newActivity) {
    if (item?.id) {
      alertCandidates.set(item.id, getActivityById(item.id) || item);
    }
  }
  for (const detection of newDetections) {
    const activityItem = getActivityById(detection.id) || detection;
    const candidateKey = activityItem?.id || detection.id || `detection:${detection.frameNumber || detection.detectedUtc || Math.random()}`;
    alertCandidates.set(candidateKey, activityItem);
  }

  const matches = [...alertCandidates.values()]
    .map((activityItem) => {
      const entries = activityItem.matchedEntries || matchDetectionToEntries(activityItem);
      return {
        detection: activityItem,
        entries,
        labels: getDetectionAlertLabels({ ...activityItem, matchedEntries: entries }),
      };
    })
    .filter((match) => match.entries.length || hasFlaggedOpcodePreference(match.detection));

  const countTriggeredMatches = changedEntryIds
    .map((entryId) => getEntryById(entryId))
    .filter((entry) => entry?.alertEnabled)
    .map((entry) => {
      const detection = [...alertCandidates.values()].find((activityItem) => activityMatchesEntry(activityItem, entry));
      if (!detection) return null;
      return {
        detection,
        entries: [entry],
        labels: getDetectionAlertLabels({ ...detection, matchedEntries: [entry] }),
      };
    })
    .filter(Boolean);

  const allMatches = [];
  const seenAlertKeys = new Set();
  for (const match of [...matches, ...countTriggeredMatches]) {
    const entryIds = match.entries.map((entry) => entry.id).sort().join(",");
    const key = `${match.detection.id || match.detection.frameNumber || match.detection.detectedUtc}|${entryIds}`;
    if (seenAlertKeys.has(key)) continue;
    seenAlertKeys.add(key);
    allMatches.push(match);
  }

  if (!allMatches.length) return;

  markEntriesRecentlyDetected(allMatches.flatMap((match) => match.entries.map((entry) => entry.id)));
  state.currentAlertMatches = [...state.currentAlertMatches, ...allMatches];
  renderTable();
  renderDetectionAlertModal();
  await playAlertTone();
}

function updateSelectedBookmark(patch) {
  if (!state.selectedBookmarkId) return;
  state.bookmarkState[state.selectedBookmarkId] = {
    ...(state.bookmarkState[state.selectedBookmarkId] || {}),
    ...patch,
    updatedAt: new Date().toISOString(),
  };
  persistState();
  renderBookmarks();
  renderBookmarkModal();
}

  async function resetMarkers() {
    if (!confirm("Clear the current session's Interaction Markers and their local notes, expected opcodes, and links?")) return;
    clearCurrentBookmarkState();
    state.selectedBookmarkId = null;
    state.bookmarkPagination.page = 1;
    persistState();
    setBookmarkModal(false);
    if (!state.liveSession.sessionName) {
      renderBookmarks();
      renderBookmarkModal();
      setSessionActionMessage("Cleared local marker annotations.", "success");
      return;
    }
    await runSessionAction("clearMarkers");
  }

function updateSelectedEntry(patch) {
  if (!state.selectedId) return;
  state.entryState[state.selectedId] = {
    ...(state.entryState[state.selectedId] || {}),
    ...patch,
    updatedAt: new Date().toISOString(),
  };
  invalidateEntryCaches();
  persistState();
  renderStats();
  renderTable();
  renderBookmarks();
  renderInspector();
}

function stepSelectedWorkflow(direction) {
  const entry = getEntryById(state.selectedId);
  if (!entry) return;
  const currentIndex = getWorkflowStageIndex(entry.workflowStage);
  const nextIndex = Math.max(0, Math.min(WORKFLOW_STAGES.length - 1, currentIndex + direction));
  if (nextIndex === currentIndex) return;
  updateSelectedEntry(getWorkflowStagePatch(WORKFLOW_STAGES[nextIndex]));
}

function updateSelectedCustomEntryFields(patch) {
  const entry = getEntryById(state.selectedId);
  if (!entry || entry.source_type !== "custom") return;
  state.customEntries = state.customEntries.map((item) => (
    item.id === entry.id
      ? { ...item, ...patch }
      : item
  ));
  invalidateEntryCaches();
  persistState();
  renderStats();
  renderTable();
  renderBookmarks();
  renderInspector();
}

function updateSelectedCustomIdentityField(field, rawValue) {
  const entry = getEntryById(state.selectedId);
  if (!entry || entry.source_type !== "custom") return;

  let nextValue = String(rawValue ?? "").trim();
  if (field === "rof2_opcode" || field === "test_opcode") {
    nextValue = normalizeOpcode(nextValue);
  }

  if (String(entry[field] || "") === String(nextValue || "")) return;
  updateSelectedCustomEntryFields({ [field]: nextValue });
}

function recordSuggestedWorkflowHypothesis() {
  const entry = getEntryById(state.selectedId);
  if (!entry) return;
  const analysis = buildWorkflowAnalysis(entry);
  if (!analysis.bestCandidate) return;
  const hypothesisLine = [
    `Hypothesis: ${analysis.bestCandidate.opcode}`,
    analysis.bestCandidate.names?.length ? analysis.bestCandidate.names.join(", ") : "",
    analysis.bestCandidate.rationale || "",
  ].filter(Boolean).join(" • ");
  updateSelectedCustomEntryFields({
    test_opcode: analysis.bestCandidate.opcode,
    extra: [
      entry.extra || "",
      hypothesisLine,
    ].filter(Boolean).join("\n"),
  });
  updateSelectedEntry({
    workflowHypothesis: true,
    workflowStage: getWorkflowStageIndex(entry.workflowStage) < getWorkflowStageIndex("opcode-hypothesis")
      ? "opcode-hypothesis"
      : entry.workflowStage,
  });
}

function promoteWorkflowHypothesis() {
  const entry = getEntryById(state.selectedId);
  if (!entry || entry.source_type !== "custom" || !entry.test_opcode) return;
  updateSelectedCustomEntryFields({
    rof2_opcode: entry.test_opcode,
  });
}

function addCustomEntry() {
  const opcode = normalizeOpcode(els.customOpcode.value);
  const name = els.customName.value.trim();
  const packetSignature = els.customPacketSignature.value.trim();
  const packetFamilyKey = normalizePacketFamilyKey(els.customPacketFamilyKey.value);
  const confidence = els.customConfidence.value || "medium";
  const notes = els.customNotes.value.trim();
  if (!opcode && !name && !packetSignature && !packetFamilyKey) {
    alert("Provide at least a name, opcode, packet signature, or packet family for the custom entry.");
    return;
  }

  const id = `custom-${Date.now()}`;
  state.customEntries.unshift({
    id,
    sheet_row: null,
    rof2_opcode: opcode,
    rof2_name: name,
    eqemu_name: "",
    display_name: name || opcode,
    notes: "",
    extra: packetSignature ? `Packet Signature: ${packetSignature}` : "",
    packet_signature: packetSignature,
    packet_family_key: packetFamilyKey,
    test_opcode: "",
  });
  state.entryState[id] = {
    status: "suspected",
    confidence,
    workflowStage: "candidate",
    workflowRepeatable: false,
    workflowIsolated: false,
    workflowHypothesis: Boolean(opcode),
    workflowConfirmed: false,
    tags: "",
    userNotes: notes,
    updatedAt: new Date().toISOString(),
  };
  invalidateEntryCaches();
  state.selectedId = id;
  els.customOpcode.value = "";
  els.customName.value = "";
  els.customPacketSignature.value = "";
  els.customPacketFamilyKey.value = "";
  els.customConfidence.value = "medium";
  els.customNotes.value = "";
  setCustomEntryDrawer(false);
  persistState();
  renderStats();
  renderTable();
  renderBookmarks();
  renderInspector();
}

function deleteSelectedCustomEntry() {
  const entry = getEntryById(state.selectedId);
  if (!entry || entry.source_type !== "custom") return;
  state.customEntries = state.customEntries.filter((item) => item.id !== entry.id);
  delete state.entryState[entry.id];
  invalidateEntryCaches();
  state.selectedId = null;
  setInspectorModal(false);
  persistState();
  renderStats();
  renderTable();
  renderInspector();
}

function navigateInspector(direction) {
  if (!state.inspectorOpen || !state.selectedId) return;
  const entries = getVisibleEntries();
  const currentIndex = entries.findIndex((e) => e.id === state.selectedId);
  if (currentIndex === -1) return;
  const nextIndex = currentIndex + direction;
  if (nextIndex < 0 || nextIndex >= entries.length) return;
  state.selectedId = entries[nextIndex].id;
  renderTable();
  renderBookmarks();
  renderInspector();
}

async function copySelectedSummary() {
  const entry = getEntryById(state.selectedId);
  if (!entry) return;
  const checklist = getWorkflowChecklist(entry);
  const summary = [
    `Opcode: ${entry.rof2_opcode || "n/a"}`,
    `Reference Name: ${entry.rof2_name || "n/a"}`,
    `EQEmu Name: ${entry.eqemu_name || "n/a"}`,
    `Status: ${getStatusLabel(entry.status)}`,
    `Confidence: ${entry.confidence}`,
    `Workflow Stage: ${WORKFLOW_STAGE_LABELS[entry.workflowStage] || entry.workflowStage}`,
    `Workflow Checks: repeatable=${checklist.repeatable} isolated=${checklist.isolated} hypothesis=${checklist.hypothesis} confirmed=${checklist.confirmed}`,
    `Tags: ${entry.tags || "n/a"}`,
    `Notes: ${entry.userNotes || "n/a"}`,
  ].join("\n");
  await navigator.clipboard.writeText(summary);
  els.copySummaryButton.textContent = "Copied!";
  els.copySummaryButton.classList.add("button-copied");
  setTimeout(() => {
    els.copySummaryButton.textContent = "Copy Row Summary";
    els.copySummaryButton.classList.remove("button-copied");
  }, 1500);
}

function exportState() {
  const blob = new Blob([JSON.stringify({
    version: 1,
    exportedAt: new Date().toISOString(),
    entryState: state.entryState,
    bookmarkState: state.bookmarkState,
    customEntries: state.customEntries,
    opcodePreferences: state.opcodePreferences,
    packetPreferences: state.packetPreferences,
    suppressionRules: state.suppressionRules,
  }, null, 2)], { type: "application/json" });
  const link = document.createElement("a");
  link.href = URL.createObjectURL(blob);
  link.download = "occ-state-rof2.json";
  link.click();
  URL.revokeObjectURL(link.href);
}

function importState(file) {
  const reader = new FileReader();
  reader.onload = () => {
    try {
      const parsed = JSON.parse(String(reader.result));
      state.entryState = parsed.entryState || {};
      state.bookmarkState = parsed.bookmarkState || {};
      state.customEntries = parsed.customEntries || [];
      state.opcodePreferences = parsed.opcodePreferences || {};
      state.packetPreferences = parsed.packetPreferences || {};
      state.suppressionRules = parsed.suppressionRules || {};
      invalidateEntryCaches();
      invalidatePreferenceCaches();
      invalidateSuppressionCaches();
      persistState();
      renderStats();
      renderTable();
      renderLiveSession();
      renderBookmarks();
      renderInspector();
    } catch (error) {
      alert(`Import failed: ${error.message}`);
    }
  };
  reader.readAsText(file);
}

function attachEvents() {
  const resetToFirstPage = () => { state.pagination.page = 1; };
  document.addEventListener("pointerdown", () => { primeAudioContext().catch(() => {}); });
  els.toggleCustomEntryButton.addEventListener("click", () => {
    setSettingsDrawer(false);
    setCustomEntryDrawer(els.customEntryDrawer.hidden, { overlay: false });
  });
  els.toggleSettingsButton.addEventListener("click", () => {
    setCustomEntryDrawer(false);
    setSettingsDrawer(els.settingsDrawer.hidden);
  });
  document.addEventListener("click", (event) => {
    if (els.customEntryDrawer.hidden) return;
    if (Date.now() < state.ignoreCustomEntryOutsideClickUntil) return;
    if (els.customEntryDrawer.contains(event.target) || els.toggleCustomEntryButton.contains(event.target)) return;
    setCustomEntryDrawer(false);
  });
  document.addEventListener("click", (event) => {
    if (els.settingsDrawer.hidden) return;
    if (els.settingsDrawer.contains(event.target) || els.toggleSettingsButton.contains(event.target)) return;
    setSettingsDrawer(false);
  });
  document.addEventListener("click", (event) => {
    if (els.liveMonitorContextMenu.hidden) return;
    if (els.liveMonitorContextMenu.contains(event.target)) return;
    closeLiveMonitorContextMenu();
  });
  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape" && !els.detectionAlertModal.hidden) {
      dismissDetectionAlerts();
      return;
    }
    if (event.key === "Escape" && state.renameOpcodeModalOpen) {
      setRenameOpcodeModal(false);
      return;
    }
    if (event.key === "Escape" && state.liveMonitorContextMenu.open) {
      closeLiveMonitorContextMenu();
      return;
    }
    if (event.key === "Escape" && state.liveMonitorOpen) {
      setLiveMonitorModal(false);
      return;
    }
    if (event.key === "Escape" && state.bookmarkModalOpen) {
      setBookmarkModal(false);
      return;
    }
    if (event.key === "Escape" && state.workflowModalOpen) {
      setWorkflowModal(false);
      return;
    }
    if (event.key === "Escape" && state.inspectorOpen) {
      setInspectorModal(false);
      return;
    }
    if (event.key === "Escape" && !els.settingsDrawer.hidden) {
      setSettingsDrawer(false);
      return;
    }
    if (event.key === "Escape" && !els.customEntryDrawer.hidden) {
      setCustomEntryDrawer(false);
      return;
    }
    if (state.inspectorOpen && (event.key === "ArrowLeft" || event.key === "ArrowRight")) {
      const target = event.target;
      if (target.tagName === "INPUT" || target.tagName === "TEXTAREA" || target.tagName === "SELECT") return;
      event.preventDefault();
      navigateInspector(event.key === "ArrowLeft" ? -1 : 1);
    }
  });
  els.closeInspectorButton.addEventListener("click", () => setInspectorModal(false));
  els.openWorkflowButton.addEventListener("click", () => setWorkflowModal(true));
  els.closeWorkflowButton.addEventListener("click", () => setWorkflowModal(false));
  els.workflowModal.addEventListener("click", (event) => {
    if (event.target === els.workflowModal || event.target.classList.contains("modal-backdrop")) {
      setWorkflowModal(false);
    }
  });
  els.closeBookmarkModalButton.addEventListener("click", () => setBookmarkModal(false));
  els.closeLiveMonitorButton.addEventListener("click", () => setLiveMonitorModal(false));
  els.closeRenameOpcodeButton.addEventListener("click", () => setRenameOpcodeModal(false));
  els.dismissDetectionAlertButton.addEventListener("click", dismissDetectionAlerts);
  els.dismissDetectionAlertFooterButton.addEventListener("click", dismissDetectionAlerts);
  els.openDetectionEntryButton.addEventListener("click", openFirstDetectionAlertEntry);
  els.prevEntryButton.addEventListener("click", () => navigateInspector(-1));
  els.nextEntryButton.addEventListener("click", () => navigateInspector(1));
  els.inspectorModal.addEventListener("click", (event) => {
    if (event.target === els.inspectorModal || event.target.classList.contains("modal-backdrop")) {
      setInspectorModal(false);
    }
  });
  els.bookmarkModal.addEventListener("click", (event) => {
    if (event.target === els.bookmarkModal || event.target.classList.contains("modal-backdrop")) {
      setBookmarkModal(false);
    }
  });
  els.liveMonitorModal.addEventListener("click", (event) => {
    if (event.target === els.liveMonitorModal || event.target.classList.contains("modal-backdrop")) {
      setLiveMonitorModal(false);
    }
  });
  els.renameOpcodeModal.addEventListener("click", (event) => {
    if (event.target === els.renameOpcodeModal || event.target.classList.contains("modal-backdrop")) {
      setRenameOpcodeModal(false);
    }
  });
  els.detectionAlertModal.addEventListener("click", (event) => {
    if (event.target === els.detectionAlertModal || event.target.classList.contains("modal-backdrop")) {
      dismissDetectionAlerts();
    }
  });
  els.searchInput.addEventListener("input", (event) => { state.filters.search = event.target.value; resetToFirstPage(); renderTable(); });
  els.statusFilter.addEventListener("change", (event) => { state.filters.status = event.target.value; resetToFirstPage(); renderTable(); });
  els.sourceFilter.addEventListener("change", (event) => { state.filters.source = event.target.value; resetToFirstPage(); renderTable(); });
  els.sortFilter.addEventListener("change", (event) => {
    setSort(event.target.value);
    resetToFirstPage();
    renderTable();
  });
  for (const button of els.sortButtons) {
    button.addEventListener("click", () => {
      setSort(button.dataset.sort, { toggle: true });
      resetToFirstPage();
      renderTable();
    });
  }
  els.trackedOnlyToggle.addEventListener("change", (event) => { state.filters.trackedOnly = event.target.checked; resetToFirstPage(); renderTable(); });
  els.clearFiltersButton.addEventListener("click", () => {
    state.filters = { search: "", status: "all", source: "all", sort: "tracked", sortDirection: "desc", trackedOnly: false };
    state.pagination.page = 1;
    els.searchInput.value = "";
    els.statusFilter.value = "all";
    els.sourceFilter.value = "all";
    els.trackedOnlyToggle.checked = false;
    syncSortUi();
    renderTable();
  });
  els.sessionToggleButton.addEventListener("click", () => {
    const action = state.liveSession.status === "running" ? "stop" : "start";
    runSessionAction(action).catch(() => {});
  });
  els.restartSessionButton.addEventListener("click", () => {
    if (!els.restartSessionButton.disabled) {
      restartCaptureSession().catch((error) => {
        setSessionActionMessage(error.message, "error");
      });
    }
  });
  els.openLiveMonitorButton.addEventListener("click", () => {
    if (!els.openLiveMonitorButton.disabled) {
      state.liveMonitorFilters.tab = "feed";
      setLiveMonitorModal(true);
      renderLiveMonitor();
      refreshLiveSession().catch(() => {});
    }
  });
  els.openCapturesFolderButton.addEventListener("click", () => { openCapturesFolder().catch(() => {}); });
  els.markSessionButton.addEventListener("click", () => { runSessionAction("mark").catch(() => {}); });
  els.refreshSessionButton.addEventListener("click", () => { refreshLiveSession().catch(() => {}); });
  els.rescanOpcodesButton.addEventListener("click", () => {
    if (!state.registryRescanPending) {
      refreshOpcodeRegistry().catch(() => {});
    }
  });
  els.clearLiveMonitorFeedButton.addEventListener("click", () => {
    clearLiveMonitorFeed().catch(() => {});
  });
  els.liveMonitorModeFilter.addEventListener("change", (event) => {
    state.liveMonitorFilters.mode = event.target.value;
    renderLiveMonitor();
  });
  els.liveMonitorCountLimitInput.addEventListener("input", (event) => {
    const raw = String(event.target.value || "").trim();
    if (!raw) {
      state.liveMonitorFilters.countLimit = "";
    } else {
      const parsed = Math.max(1, Math.floor(Number(raw) || 0));
      state.liveMonitorFilters.countLimit = parsed ? String(parsed) : "";
      event.target.value = state.liveMonitorFilters.countLimit;
    }
    renderLiveMonitor();
  });
  els.liveMonitorUnknownOnlyToggle.addEventListener("change", (event) => {
    state.liveMonitorFilters.unknownOnly = event.target.checked;
    renderLiveMonitor();
  });
  els.liveMonitorSearchInput.addEventListener("input", (event) => {
    state.liveMonitorFilters.search = event.target.value;
    renderLiveMonitor();
  });
  els.liveFeedTabButton.addEventListener("click", () => {
    state.liveMonitorFilters.tab = "feed";
    renderLiveMonitor();
  });
  els.suppressedTabButton.addEventListener("click", () => {
    state.liveMonitorFilters.tab = "suppressed";
    renderLiveMonitor();
  });
  els.liveMonitorList.addEventListener("click", (event) => {
    const copyButton = event.target.closest(".monitor-item-copy-button[data-copy-payload]");
    if (copyButton) {
      event.preventDefault();
      event.stopPropagation();
      copyTextToClipboard(copyButton.dataset.copyPayload).then((copied) => {
        if (!copied) return;
        copyButton.classList.add("copied");
        copyButton.setAttribute("title", "Copied");
        window.setTimeout(() => {
          copyButton.classList.remove("copied");
          copyButton.setAttribute("title", "Copy packet data");
        }, 1200);
      }).catch(() => {});
      return;
    }
    const menuButton = event.target.closest(".monitor-item-menu-button[data-detection-id]");
    if (menuButton) {
      event.preventDefault();
      event.stopPropagation();
      const detection = getActivityById(menuButton.dataset.detectionId);
      if (!detection) return;
      const rect = menuButton.getBoundingClientRect();
      openLiveMonitorContextMenu(detection, rect.right - 8, rect.bottom + 6, { groupKey: menuButton.dataset.groupKey || "" });
      return;
    }
    const card = event.target.closest(".monitor-item[data-entry-id]");
    if (!card) return;
    state.selectedId = card.dataset.entryId;
    setLiveMonitorModal(false);
    setInspectorModal(true);
    renderTable();
    renderBookmarks();
    renderInspector();
  });
  els.liveMonitorList.addEventListener("contextmenu", (event) => {
    const card = event.target.closest(".monitor-item[data-detection-id]");
    if (!card) return;
    event.preventDefault();
    const detection = getActivityById(card.dataset.detectionId);
    if (!detection) return;
    openLiveMonitorContextMenu(detection, event.clientX, event.clientY, { groupKey: card.dataset.groupKey || "" });
  });
  els.toggleLiveMonitorFlagButton.addEventListener("click", () => {
    const opcode = state.liveMonitorContextMenu.opcode;
    if (!opcode) return;
    const preference = getOpcodePreference(opcode);
    updateOpcodePreference(opcode, { flagged: !preference.flagged });
    closeLiveMonitorContextMenu();
  });
  els.renameLiveMonitorOpcodeButton.addEventListener("click", () => {
    const detection = getActivityById(state.liveMonitorContextMenu.detectionId);
    if (!detection) return;
    if (state.liveMonitorContextMenu.opcode) {
      openRenameOpcodeModal(state.liveMonitorContextMenu.opcode);
      return;
    }
    openRenamePacketModal(detection, { groupKey: state.liveMonitorContextMenu.groupKey });
  });
  els.createLiveMonitorEntryButton.addEventListener("click", () => {
    const detection = getActivityById(state.liveMonitorContextMenu.detectionId);
    if (!detection) return;
    seedCustomEntryFromDetection(detection, { groupKey: state.liveMonitorContextMenu.groupKey });
  });
  els.suppressLiveMonitorItemButton.addEventListener("click", () => {
    const detection = getActivityById(state.liveMonitorContextMenu.detectionId);
    if (!detection) return;
    const rule = buildSuppressionRule(detection, { groupKey: state.liveMonitorContextMenu.groupKey });
    if (state.suppressionRules[rule.key]) {
      closeLiveMonitorContextMenu();
      return;
    }
    updateSuppressionRule(rule);
    closeLiveMonitorContextMenu();
  });
  els.openLiveMonitorEntryButton.addEventListener("click", openContextOpcodeEntry);
  els.suppressedList.addEventListener("click", (event) => {
    const button = event.target.closest(".unsuppress-rule-button[data-rule-key]");
    if (!button) return;
    removeSuppressionRule(button.dataset.ruleKey);
  });
  els.saveRenameOpcodeButton.addEventListener("click", () => saveOpcodeAlias(els.renameOpcodeInput.value));
  els.clearRenameOpcodeButton.addEventListener("click", clearOpcodeAlias);
  els.renameOpcodeInput.addEventListener("keydown", (event) => {
    if (event.key === "Enter") {
      event.preventDefault();
      saveOpcodeAlias(event.target.value);
    }
  });
  els.prevBookmarkPageButton.addEventListener("click", () => {
    state.bookmarkPagination.page = Math.max(1, state.bookmarkPagination.page - 1);
    renderBookmarks();
  });
  els.nextBookmarkPageButton.addEventListener("click", () => {
    state.bookmarkPagination.page += 1;
    renderBookmarks();
  });
  els.prevPageButton.addEventListener("click", () => {
    state.pagination.page = Math.max(1, state.pagination.page - 1);
    renderTable();
  });
  els.nextPageButton.addEventListener("click", () => {
    state.pagination.page += 1;
    renderTable();
  });
  els.pageSizeSelect.addEventListener("change", (event) => {
    state.pagination.pageSize = Number(event.target.value) || 50;
    state.pagination.page = 1;
    renderTable();
  });
  els.bookmarkExpectedOpcode.addEventListener("input", (event) => updateSelectedBookmark({ expectedOpcode: event.target.value }));
  els.bookmarkNotes.addEventListener("input", (event) => updateSelectedBookmark({ notes: event.target.value }));
  els.linkSelectedOpcodeButton.addEventListener("click", () => updateSelectedBookmark({ linkedEntryId: state.selectedId || "" }));
  els.resetMarkersButton.addEventListener("click", resetMarkers);
  els.inspectorOpcode.addEventListener("change", (event) => updateSelectedCustomIdentityField("rof2_opcode", event.target.value));
  els.inspectorSheetName.addEventListener("change", (event) => updateSelectedCustomIdentityField("rof2_name", event.target.value));
  els.inspectorEqemuName.addEventListener("change", (event) => updateSelectedCustomIdentityField("eqemu_name", event.target.value));
  els.inspectorTestOpcode.addEventListener("change", (event) => updateSelectedCustomIdentityField("test_opcode", event.target.value));
  els.entryStatus.addEventListener("change", (event) => updateSelectedEntry({ status: event.target.value }));
  els.entryConfidence.addEventListener("change", (event) => updateSelectedEntry({ confidence: event.target.value }));
  els.entryAlertEnabled.addEventListener("change", (event) => updateSelectedEntry({ alertEnabled: event.target.checked }));
  els.entryWorkflowStage.addEventListener("change", (event) => updateSelectedEntry(getWorkflowStagePatch(event.target.value)));
  els.workflowRepeatableCheck.addEventListener("change", (event) => updateSelectedEntry({ workflowRepeatable: event.target.checked }));
  els.workflowIsolatedCheck.addEventListener("change", (event) => updateSelectedEntry({ workflowIsolated: event.target.checked }));
  els.workflowHypothesisCheck.addEventListener("change", (event) => updateSelectedEntry({ workflowHypothesis: event.target.checked }));
  els.workflowConfirmedCheck.addEventListener("change", (event) => updateSelectedEntry({ workflowConfirmed: event.target.checked }));
  els.workflowSyncStageButton.addEventListener("click", () => {
    const entry = getEntryById(state.selectedId);
    if (!entry) return;
    const analysis = buildWorkflowAnalysis(entry);
    updateSelectedEntry(getWorkflowStagePatch(analysis.suggestedStage));
  });
  els.workflowApplyHypothesisButton.addEventListener("click", recordSuggestedWorkflowHypothesis);
  els.workflowPromoteHypothesisButton.addEventListener("click", promoteWorkflowHypothesis);
  els.workflowBackButton.addEventListener("click", () => stepSelectedWorkflow(-1));
  els.workflowAdvanceButton.addEventListener("click", () => stepSelectedWorkflow(1));
  els.entryTags.addEventListener("input", (event) => updateSelectedEntry({ tags: event.target.value }));
  els.entryNotes.addEventListener("input", (event) => updateSelectedEntry({ userNotes: event.target.value }));
  els.addCustomEntryButton.addEventListener("click", addCustomEntry);
  els.exportStateButton.addEventListener("click", exportState);
  els.importStateInput.addEventListener("change", (event) => {
    const [file] = event.target.files;
    if (file) importState(file);
    event.target.value = "";
  });
  els.resetStateButton.addEventListener("click", () => {
    if (!confirm("Reset local OCC state for this browser?")) return;
    localStorage.removeItem(STORAGE_KEY);
    state.entryState = {};
    state.bookmarkState = {};
    state.customEntries = [];
    state.opcodePreferences = {};
    state.packetPreferences = {};
    state.suppressionRules = {};
    invalidateEntryCaches();
    invalidatePreferenceCaches();
    invalidateSuppressionCaches();
    state.selectedId = null;
    state.selectedBookmarkId = null;
    state.bookmarkPagination.page = 1;
    state.currentAlertMatches = [];
    state.seenDetectionIds = new Set();
    state.seenActivityIds = new Set();
    clearRecentActivityTimers();
    state.recentDetectedEntryIds = new Set();
    state.liveMonitorFilters = { tab: "feed", mode: "all", search: "", unknownOnly: false, countLimit: "" };
    state.renameOpcodeTarget = "";
    state.renamePacketTarget = "";
    setCustomEntryDrawer(false);
    setSettingsDrawer(false);
    closeLiveMonitorContextMenu();
    setDetectionAlertModal(false);
    setLiveMonitorModal(false);
    setRenameOpcodeModal(false);
    setBookmarkModal(false);
    setWorkflowModal(false);
    setInspectorModal(false);
    renderStats();
    renderTable();
    renderLiveSession();
    renderBookmarks();
    renderBookmarkModal();
    renderInspector();
  });
  els.copySummaryButton.addEventListener("click", copySelectedSummary);
  els.deleteCustomEntryButton.addEventListener("click", deleteSelectedCustomEntry);
}

async function refreshLiveSession() {
  try {
    const response = await fetch(`${API_SESSION_URL}?t=${Date.now()}`, { cache: "no-store" });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const payload = await response.json();
    applyLiveSessionPayload(payload.session || payload);
  } catch {
    applyLiveSessionPayload({ status: "idle", markers: [], sessionName: "", markerCount: 0, activityCount: 0, activity: [], detectionCount: 0, detections: [] });
  }
  await processLiveDetections();
  renderTable();
  renderLiveSession();
  renderBookmarks();
  renderBookmarkModal();
}

async function refreshOpcodeRegistry() {
  const previousCount = Array.isArray(state.data?.entries) ? state.data.entries.length : 0;
  state.registryRescanPending = true;
  els.rescanOpcodesButton.disabled = true;
  setRegistryActionMessage("Rescanning EQEmu opcodes...", "muted");

  try {
    const response = await fetch(API_RESCAN_OPCODES_URL, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: "{}",
    });
    const result = await response.json().catch(() => ({}));
    if (!response.ok || result.ok === false) {
      throw new Error(result.error || result.message || `Rescan failed with HTTP ${response.status}`);
    }

    const nextRegistry = result.registry;
    if (!nextRegistry || !Array.isArray(nextRegistry.entries)) {
      throw new Error("Rescan completed but the refreshed opcode registry payload was missing.");
    }

    remapRegistryStateForRescan(nextRegistry);
    state.data = nextRegistry;
    invalidateEntryCaches();

    if (state.selectedId && !getEntryById(state.selectedId)) {
      state.selectedId = null;
    }

    state.currentAlertMatches = [];
    state.currentSessionKey = "";
    state.countPulseSessionKey = "";
    state.detectionBootstrapComplete = false;
    state.lastSessionEntryCounts = new Map();
    clearRecentCountTimers();
    persistState();
    await processLiveDetections();
    renderStats();
    renderTable();
    renderLiveSession();
    renderBookmarks();
    renderBookmarkModal();
    renderInspector();

    const nextCount = nextRegistry.entries.length;
    const delta = nextCount - previousCount;
    const deltaLabel = delta === 0 ? "no row delta" : `${delta > 0 ? "+" : ""}${delta} row${Math.abs(delta) === 1 ? "" : "s"}`;
    setRegistryActionMessage(
      `${result.message || "EQEmu opcodes rescanned."} ${nextCount} rows loaded (${deltaLabel}).`,
      "success",
    );
  } catch (error) {
    setRegistryActionMessage(error.message || "Opcode registry rescan failed.", "error");
  } finally {
    state.registryRescanPending = false;
    els.rescanOpcodesButton.disabled = false;
  }
}

async function loadInterfaces() {
  try {
    const response = await fetch(`${API_INTERFACES_URL}?t=${Date.now()}`, { cache: "no-store" });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const payload = await response.json();
    state.interfaces = Array.isArray(payload.interfaces) ? payload.interfaces : [];
  } catch {
    state.interfaces = [{ value: "loopback", label: "Loopback", description: "Adapter for loopback traffic capture" }];
  }
  renderInterfaceOptions(getPreferredCaptureInterfaceValue(state.interfaces));
}

async function bootstrap() {
  loadPersistedState();
  renderStatusOptions();
  attachEvents();

  const response = await fetch(DATA_URL);
  state.data = await response.json();
  invalidateEntryCaches();
  await loadInterfaces();
  await refreshLiveSession();
  setCustomEntryDrawer(false);
  setSettingsDrawer(false);
  setDetectionAlertModal(false);
  setLiveMonitorModal(false);
  setInspectorModal(false);
  syncSortUi();
  els.pageSizeSelect.value = String(state.pagination.pageSize);
  els.liveMonitorModeFilter.value = state.liveMonitorFilters.mode;
  els.liveMonitorSearchInput.value = state.liveMonitorFilters.search;
  els.liveMonitorCountLimitInput.value = state.liveMonitorFilters.countLimit;
  els.liveMonitorUnknownOnlyToggle.checked = state.liveMonitorFilters.unknownOnly;
  renderStats();
  renderTable();
  renderLiveSession();
  renderBookmarks();
  renderBookmarkModal();
  renderInspector();
  window.setInterval(() => { refreshLiveSession().catch(() => {}); }, 5000);
}

bootstrap().catch((error) => {
  document.body.innerHTML = `<pre style="padding:24px">Failed to load OCC: ${error.message}</pre>`;
});
