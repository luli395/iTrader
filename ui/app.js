const API_BASE = window.location.protocol.startsWith('http')
    ? window.location.origin
    : 'http://127.0.0.1:8080';
const APP_ASSET_VERSION = '20260602_live_chart_cache_merge';

function createSampleRecorderConfig() {
    return {
        config_path: 'configs/ctp_md_recorder.ini',
        launch_script: 'scripts/start_recorder_release.ps1',
        exists: true,
        account_section: 'account.recorder',
        account_id: 'recorder',
        output_dir: '../runtime/ctp_md_recorder/agtick',
        instruments: 'ag2606',
        flush_interval_ms: '1000',
        status_interval_ms: '30000',
        idle_sleep_ms: '250',
        connect_timeout_ms: '15000',
        deduplicate_exact_ticks: 'true',
        auto_restart_enabled: 'true',
        front: 'tcp://127.0.0.1:17002',
        broker_id: '',
        user_id: '',
        password: '',
        product_info: 'iTrader',
        flow_dir: '',
        production_mode: 'true',
        reconnect_enabled: 'true',
        reconnect_retry_interval_ms: '3000',
        reconnect_max_attempts: '0'
    };
}

const sampleStates = {
    backtest: {
        mode: 'backtest',
        accounts: [
            {
                id: 'sim_a',
                initial_cash: '1000000'
            },
            {
                id: 'sim_b',
                initial_cash: '750000'
            }
        ],
        strategies: [
            {
                id: 'ma_fast',
                dll: '../build/Debug/sample_strategy.dll',
                backtest_data_dir: '',
                accounts: ['sim_a'],
                runtime_details: {
                    sim_a: {
                        positions: [
                            { instrument: 'AG2602', account_id: 'sim_a', strategy_id: 'ma_fast', long_today_quantity: 1, long_yesterday_quantity: 0, long_quantity: 1, long_average_price: '12475.00', short_today_quantity: 0, short_yesterday_quantity: 0, short_quantity: 0, short_average_price: '0.00', net: 1, average_price: '12475.00' }
                        ],
                        opened_orders: [],
                        closed_orders: [
                            { order_id: 'sim_a-1', source_order_id: '', account_id: 'sim_a', strategy_id: 'ma_fast', instrument: 'AG2602', side: 'buy', offset: 'open', requested_volume: 1, filled_volume: 1, limit_price: '12475.00', filled_price: '12475.00', status: 'filled', message: 'filled by Uft-style backtest matcher', timestamp: '2025-11-27 21:00:14' }
                        ],
                        warnings: []
                    }
                },
                instruments: 'AG2602',
                fast_window: '4',
                slow_window: '9',
                quantity: '1'
            },
            {
                id: 'ma_slow',
                dll: '../build/Debug/sample_strategy.dll',
                backtest_data_dir: '',
                accounts: ['sim_b'],
                runtime_details: {
                    sim_b: {
                        positions: [
                            { instrument: 'AG2602', account_id: 'sim_b', strategy_id: 'ma_slow', long_today_quantity: 0, long_yesterday_quantity: 0, long_quantity: 0, long_average_price: '0.00', short_today_quantity: 1, short_yesterday_quantity: 0, short_quantity: 1, short_average_price: '12466.00', net: -1, average_price: '12466.00' }
                        ],
                        opened_orders: [],
                        closed_orders: [
                            { order_id: 'sim_b-1', source_order_id: '', account_id: 'sim_b', strategy_id: 'ma_slow', instrument: 'AG2602', side: 'sell', offset: 'open', requested_volume: 1, filled_volume: 1, limit_price: '12466.00', filled_price: '12466.00', status: 'filled', message: 'filled by Uft-style backtest matcher', timestamp: '2025-11-27 21:00:18' }
                        ],
                        warnings: []
                    }
                },
                instruments: 'AG2602',
                fast_window: '6',
                slow_window: '12',
                quantity: '1'
            }
        ],
        backtest: {
            data_dir: './data/ticks',
            csv: ''
        },
        live: {
            environment: 'simnow',
            poll_interval_ms: '1000',
            iterations: '0',
            dry_run: 'false'
        },
        live_inventory: {
            adjustments_path: '',
            store_path: '',
            persisted_positions: [],
            adjustments: [],
            reconciliations: [],
            warnings: []
        },
        live_runtime: {
            status: 'stopped',
            running: false,
            stop_requested: false,
            process_id: 0,
            exit_code: 0,
            executable_path: '',
            active_config_path: '',
            requested_config_path: '',
            config_matches_request: true,
            started_at_ms: 0,
            finished_at_ms: 0,
            message: ''
        },
        recorder: createSampleRecorderConfig(),
        recorder_runtime: {
            status: 'stopped',
            running: false,
            stop_requested: false,
            process_id: 0,
            exit_code: 0,
            managed_by: '',
            controller_name: '',
            executable_path: '',
            config_path: 'configs/ctp_md_recorder.ini',
            started_at_ms: 0,
            finished_at_ms: 0,
            message: ''
        },
        equity: [1000000, 1000030, 999995, 1000070, 1000150, 1000205],
        activity: [
            { time: '08:45', text: 'Loaded an AGTICK-style backtest profile using the shared tick folder default.' },
            { time: '08:47', text: 'Prepared two simulation accounts with silver futures instruments from AGTICK.' },
            { time: '08:49', text: 'Ready to export an INI file or launch the CLI backtest against the configured backtest data dir.' }
        ]
    },
    live: {
        mode: 'live',
        accounts: [
            {
                id: 'ctp_demo',
                front: 'tcp://127.0.0.1:17001',
                md_front: 'tcp://127.0.0.1:17002',
                broker_id: '',
                user_id: '',
                investor_id: '',
                password: '',
                app_id: '',
                auth_code: '',
                product_info: 'iTrader',
                flow_dir: '',
                md_flow_dir: '',
                production_mode: 'true',
                reconnect_enabled: 'true',
                reconnect_retry_interval_ms: '3000',
                reconnect_max_attempts: '0',
                initial_cash: '0'
            }
        ],
        strategies: [
            {
                id: 'ma_live',
                dll: '../build/Debug/sample_strategy.dll',
                accounts: ['ctp_demo'],
                runtime_details: {
                    ctp_demo: {
                        positions: [],
                        opened_orders: [],
                        closed_orders: [],
                        warnings: ['Live runtime detail telemetry requires a connected engine feed.']
                    }
                },
                instruments: 'IF2506',
                fast_window: '3',
                slow_window: '8',
                quantity: '1'
            }
        ],
        backtest: {
            data_dir: '',
            csv: ''
        },
        live: {
            environment: 'simnow',
            poll_interval_ms: '1000',
            iterations: '0',
            dry_run: 'false'
        },
        live_inventory: {
            adjustments_path: '../runtime/strategy_inventory_adjustments.ini',
            store_path: '../runtime/strategy_inventory_store.ini',
            persisted_positions: [],
            adjustments: [],
            reconciliations: [],
            warnings: ['Manual inventory adjustments will appear here once the live API is available.']
        },
        live_runtime: {
            status: 'stopped',
            running: false,
            stop_requested: false,
            process_id: 0,
            exit_code: 0,
            executable_path: '',
            active_config_path: '',
            requested_config_path: '',
            config_matches_request: true,
            started_at_ms: 0,
            finished_at_ms: 0,
            message: ''
        },
        recorder: createSampleRecorderConfig(),
        recorder_runtime: {
            status: 'stopped',
            running: false,
            stop_requested: false,
            process_id: 0,
            exit_code: 0,
            executable_path: '',
            config_path: 'configs/ctp_md_recorder.ini',
            started_at_ms: 0,
            finished_at_ms: 0,
            message: ''
        },
        equity: [0, 8, 12, 6, 19, 24, 21],
        activity: [
            { time: '09:01', text: 'Loaded sample live profile with trader and MD fronts.' },
            { time: '09:03', text: 'Streaming quote path is enabled when the MD SDK is present.' },
            { time: '09:05', text: 'Review credentials, then export configs and launch live mode carefully.' }
        ]
    }
};

const state = structuredClone(sampleStates.backtest);
const runtime = {
    apiConnected: false,
    apiBase: API_BASE,
    sourceConfig: 'sample-state',
    lastMessage: 'Using built-in sample data until the local API responds.',
    backtestCompletionVisible: false,
    backtestCompletionTitle: 'Backtesting Done',
    backtestCompletionMessage: '',
    backtestCompletionMeta: '',
    pendingBacktestDirectoryBrowse: false,
    pendingBacktestProgress: 0,
    pendingBacktestStage: '',
    pendingBacktestStartedAt: 0,
    pendingBacktestElapsedMs: 0,
    pendingBacktestProcessedFiles: 0,
    pendingBacktestTotalFiles: 0,
    pendingBacktestProcessedTicks: 0,
    pendingBacktestDetailLevel: 'summary',
    pendingBacktestProgressTimer: null,
    pendingBacktestAbortController: null,
    pendingBacktestJobId: '',
    pendingBacktestPollTimer: null,
    pendingBacktestStrategyId: '',
    pendingBacktestAccountId: '',
    pendingLiveInventorySave: false,
    pendingLiveInventoryRefresh: false,
    pendingLiveControlAction: false,
    pendingLiveControlKey: '',
    liveRuntimePollTimer: null,
    liveStateEventSource: null,
    liveStateStreamConnected: false,
    liveStateLastStateAtMs: 0,
    liveStateReconnectTimer: null,
    liveStateReconnectAttempt: 0,
    pendingRecorderControlAction: false,
    recorderRuntimePollTimer: null,
    strategyFileCatalog: [],
    strategyFileCatalogRoot: '../strategies/bin',
    strategyFileCatalogKey: '',
    strategyFileCatalogStatus: 'idle',
    strategyFileCatalogError: '',
    pendingStrategyUpload: false
};
const pageParams = new URLSearchParams(window.location.search);
const requestedConfig = (pageParams.get('config') || '').trim();
const restoredSavedStrategyPresetKey = (() => {
    try {
        const value = window.sessionStorage.getItem('itrader:lastSavedStrategyPresetKey') || '';
        window.sessionStorage.removeItem('itrader:lastSavedStrategyPresetKey');
        return value;
    } catch (error) {
        return '';
    }
})();
if (pageParams.has('_ui_reload') || pageParams.has('_saved_preset') || pageParams.has('_saved_at')) {
    const cleanedUrl = new URL(window.location.href);
    cleanedUrl.searchParams.delete('_ui_reload');
    cleanedUrl.searchParams.delete('_saved_preset');
    cleanedUrl.searchParams.delete('_saved_at');
    window.history.replaceState(null, document.title, cleanedUrl.toString());
}
const DEFAULT_CONFIG_BY_MODE = Object.freeze({
    live: 'live.example.ini',
    backtest: 'backtest.ini'
});

function defaultConfigNameForMode(mode) {
    return mode === 'live' ? DEFAULT_CONFIG_BY_MODE.live : DEFAULT_CONFIG_BY_MODE.backtest;
}

function defaultConfigPathForMode(mode) {
    return `configs/${defaultConfigNameForMode(mode)}`;
}

function effectiveRequestedConfig(mode = state.mode) {
    return requestedConfig || defaultConfigNameForMode(mode);
}
const uiState = {
    collapsedStrategies: new Set(),
    expandedAccountStrategies: new Set(),
    accountStrategyTabs: new Map(),
    accountSummaryTabs: new Map(),
    expandedLiveInventoryAccounts: new Set(),
    recorderPanelCollapsed: true,
    activeAccountConfig: null,
    activeStrategyPickerAccount: null,
    pendingStrategyBrowseAccount: null,
    pendingAccountStrategyReveal: null,
    pendingBacktestRunKey: null,
    strategyManualPath: '',
    strategyParameterDrafts: new Map(),
    localStrategyFieldEdits: new Map(),
    strategyFieldBaselines: new Map(),
    localStrategiesDraft: null,
    localLiveInventoryDraft: null,
    localPersistedInventoryDraft: null,
    localRecorderDraft: null,
    pendingStrategyPresetSaveKey: '',
    lastSavedStrategyPresetKey: restoredSavedStrategyPresetKey,
    savedStrategyPresetKeys: new Set(restoredSavedStrategyPresetKey ? [restoredSavedStrategyPresetKey] : []),
    strategyPresetSaveErrors: new Map()
};

uiState.chartInstrument = '';
uiState.chartAccount = 'all';
uiState.chartStrategy = 'all';

const pageTitle = document.getElementById('page-title');
const metrics = document.getElementById('metrics');
const accountsList = document.getElementById('accounts-list');
const strategiesList = document.getElementById('strategies-list');
const configPreview = document.getElementById('config-preview');
const backtestSourceSection = document.getElementById('backtest-source-section');
const backtestSourceForm = document.getElementById('backtest-source-form');
const backtestPerformanceSection = document.getElementById('backtest-performance-section');
const backtestPerformanceContent = document.getElementById('backtest-performance-content');
const liveExecutionSection = document.getElementById('live-execution-section');
const liveExecutionForm = document.getElementById('live-execution-form');
const recorderConfigSection = document.getElementById('recorder-config-section');
const recorderConfigForm = document.getElementById('recorder-config-form');
const recorderConfigPreview = document.getElementById('recorder-config-preview');
const runtimeLogSection = document.getElementById('runtime-log-section');
const runtimeLogContent = document.getElementById('runtime-log-content');
const toggleRecorderPanelButton = document.getElementById('toggle-recorder-panel-button');
const saveRecorderConfigButton = document.getElementById('save-recorder-config-button');
const startRecorderButton = document.getElementById('start-recorder-button');
const stopRecorderButton = document.getElementById('stop-recorder-button');
const statusPill = document.getElementById('status-pill');
const statusCaption = document.getElementById('status-caption');
const backtestProgress = document.getElementById('backtest-progress');
const backtestProgressLabel = document.getElementById('backtest-progress-label');
const backtestProgressFiles = document.getElementById('backtest-progress-files');
const backtestProgressValue = document.getElementById('backtest-progress-value');
const backtestProgressBar = document.getElementById('backtest-progress-bar');
const backtestProgressElapsed = document.getElementById('backtest-progress-elapsed');
const cancelBacktestButton = document.getElementById('cancel-backtest-button');
const chartTitle = document.getElementById('chart-title');
const chartNote = document.getElementById('chart-note');
const chartInstrumentTabs = document.getElementById('chart-instrument-tabs');
const chartAccountFilter = document.getElementById('chart-account-filter');
const chartStrategyTabs = document.getElementById('chart-strategy-tabs');
const tradingChart = document.getElementById('trading-chart');
const accountTemplate = document.getElementById('account-template');
const strategyTemplate = document.getElementById('strategy-template');
const strategyCatalogButton = document.getElementById('add-strategy-button');
const strategyUploadButton = document.getElementById('upload-strategy-button');
const strategyUploadInput = document.getElementById('upload-strategy-input');
const accountConfigModal = document.getElementById('account-config-modal');
const accountConfigTitle = document.getElementById('account-config-title');
const accountConfigSubtitle = document.getElementById('account-config-subtitle');
const accountConfigForm = document.getElementById('account-config-form');
const strategyPickerModal = document.getElementById('strategy-picker-modal');
const strategyPickerTitle = document.getElementById('strategy-picker-title');
const strategyPickerSubtitle = document.getElementById('strategy-picker-subtitle');
const strategyPickerBody = document.getElementById('strategy-picker-body');
const backtestDoneModal = document.getElementById('backtest-done-modal');
const backtestDoneTitle = document.getElementById('backtest-done-title');
const backtestDoneMessage = document.getElementById('backtest-done-message');
const backtestDoneMeta = document.getElementById('backtest-done-meta');
const backtestDoneOkButton = document.getElementById('backtest-done-ok-button');
const chartRuntime = {
    instance: null,
    candleSeries: null,
    indicatorSeries: [],
    lastRenderKey: '',
    hasFittedContent: false,
    stableInstrumentCache: new Map(),
    lastSeriesIdentityKey: '',
    lastBarData: [],
    lastIndicatorData: new Map(),
    lastMarkerKey: '',
    lastOverlayMarkerKey: '',
    markerLayer: null,
    markerRedrawHandler: null,
    currentMarkerPayload: [],
    lastWidth: 0,
    lastHeight: 0
};

const renderRuntime = {
    scheduled: false,
    lastAtMs: 0,
    minIntervalMs: 120,
    deferredWhileSelectActive: false,
    allowFocusedSelectRender: false,
    allowFocusedSelectRenderTimer: null
};
const renderSectionCache = {
    accountsKey: '',
    runtimeLogScopeKey: '',
    runtimeLogKey: '',
    runtimeLogEntries: []
};

const sectionMap = {
    overview: document.getElementById('overview-section'),
    accounts: document.getElementById('accounts-section'),
    strategies: document.getElementById('strategies-section'),
    config: document.getElementById('config-section'),
    activity: document.getElementById('activity-section')
};

function buildModeQuery(mode) {
    const query = new URLSearchParams({ mode });
    if (requestedConfig) {
        query.set('config', requestedConfig);
    }
    return query;
}

async function fetchWithTimeout(resource, options = {}, timeoutMs = 15000) {
    const controller = new AbortController();
    const timeoutId = window.setTimeout(() => controller.abort(), timeoutMs);
    try {
        return await fetch(resource, {
            ...options,
            signal: controller.signal
        });
    } finally {
        window.clearTimeout(timeoutId);
    }
}

const accountFieldGroups = {
    backtest: [
        ['id', 'Account ID'],
        ['initial_cash', 'Initial Cash']
    ],
    live: [
        ['id', 'Account ID'],
        ['front', 'Trader Front'],
        ['md_front', 'MD Front'],
        ['broker_id', 'Broker ID'],
        ['user_id', 'User ID'],
        ['investor_id', 'Investor ID'],
        ['password', 'Password'],
        ['app_id', 'App ID'],
        ['auth_code', 'Auth Code'],
        ['product_info', 'Product Info'],
        ['flow_dir', 'Trader Flow Dir'],
        ['md_flow_dir', 'MD Flow Dir'],
        ['production_mode', 'Production Mode'],
        ['reconnect_enabled', 'Reconnect Enabled'],
        ['reconnect_retry_interval_ms', 'Reconnect Retry (ms)'],
        ['reconnect_max_attempts', 'Reconnect Max Attempts'],
        ['initial_cash', 'Initial Cash']
    ]
};

const recorderFieldGroups = [
    { key: 'config_path', label: 'Config Path', readonly: true, fullWidth: true },
    { key: 'launch_script', label: 'Launch Script', readonly: true, fullWidth: true },
    { key: 'account_id', label: 'Account ID' },
    { key: 'front', label: 'MD Front', fullWidth: true },
    { key: 'broker_id', label: 'Broker ID' },
    { key: 'user_id', label: 'User ID' },
    { key: 'password', label: 'Password', inputType: 'password', fullWidth: true },
    { key: 'product_info', label: 'Product Info', fullWidth: true },
    { key: 'output_dir', label: 'Output Dir', fullWidth: true },
    { key: 'flow_dir', label: 'MD Flow Dir', fullWidth: true },
    { key: 'instruments', label: 'Instruments', fullWidth: true },
    { key: 'flush_interval_ms', label: 'Flush Interval (ms)' },
    { key: 'status_interval_ms', label: 'Status Interval (ms)' },
    { key: 'idle_sleep_ms', label: 'Idle Sleep (ms)' },
    { key: 'connect_timeout_ms', label: 'Connect Timeout (ms)' },
    { key: 'deduplicate_exact_ticks', label: 'Deduplicate Exact Ticks', options: ['true', 'false'] },
    { key: 'auto_restart_enabled', label: 'Auto Restart', options: ['true', 'false'] },
    { key: 'production_mode', label: 'Production Mode', options: ['true', 'false'] },
    { key: 'reconnect_enabled', label: 'Reconnect Enabled', options: ['true', 'false'] },
    { key: 'reconnect_retry_interval_ms', label: 'Reconnect Retry (ms)' },
    { key: 'reconnect_max_attempts', label: 'Reconnect Max Attempts' }
];

const strategyFieldLabels = {
    id: 'Strategy ID',
    dll: 'Remote Path',
    backtest_data_dir: 'Backtest Data Dir',
    __runtimeStatus: 'Runtime Status',
    __runtimeError: 'Last Error',
    instruments: 'Instruments',
    fast_window: 'Fast Window',
    slow_window: 'Slow Window',
    quantity: 'Quantity',
    commission: 'Commission',
    fill_delay_ms: 'Fill Delay (ms)',
    matching_mode: 'Matching Mode',
    mb_bh: 'MB BH',
    mb_dh: 'MB DH',
    mb_fast: 'MB Fast',
    mb_slow: 'MB Slow',
    multiplier: 'Multiplier',
    price_scale: 'Price Scale',
    queue_ratio: 'Queue Ratio',
    tick_size: 'Tick Size'
};

const hiddenStrategyFieldKeys = new Set(['accounts', 'account', 'runtime_details']);
const readonlyStrategyFieldKeys = new Set(['__runtimeStatus', '__runtimeError']);
const suppressedLegacySchemaFieldKeys = new Set(['fast_window', 'slow_window']);
const liveForcedStrategyFieldValues = Object.freeze({
    fill_delay_ms: '0'
});
const preferredStrategyFieldOrder = [
    'id',
    'dll',
    'backtest_data_dir',
    '__runtimeStatus',
    '__runtimeError',
    'instruments',
    'commission',
    'fill_delay_ms',
    'matching_mode',
    'mb_bh',
    'mb_dh',
    'mb_fast',
    'mb_slow',
    'multiplier',
    'price_scale',
    'fast_window',
    'slow_window',
    'quantity',
    'queue_ratio',
    'tick_size'
];

function formatStrategyFieldLabel(key) {
    if (strategyFieldLabels[key]) {
        return strategyFieldLabels[key];
    }

    return String(key)
        .split('_')
        .filter(Boolean)
        .map((part) => (/^[a-z]{1,3}$/i.test(part)
            ? part.toUpperCase()
            : `${part.charAt(0).toUpperCase()}${part.slice(1)}`))
        .join(' ');
}

function isRenderableStrategyField(key, value) {
    if (hiddenStrategyFieldKeys.has(key)) {
        return false;
    }

    if (String(key).startsWith('__') && !readonlyStrategyFieldKeys.has(key)) {
        return false;
    }

    return typeof value !== 'object' && typeof value !== 'function';
}

function getForcedStrategyFieldValue(key) {
    if (state.mode !== 'live') {
        return null;
    }

    return Object.prototype.hasOwnProperty.call(liveForcedStrategyFieldValues, key)
        ? liveForcedStrategyFieldValues[key]
        : null;
}

function isModeSuppressedStrategyField(key) {
    return getForcedStrategyFieldValue(key) !== null;
}

function getStrategyFieldGroups(strategy) {
    const groups = [];
    const schemaKeys = new Set(getStrategySchemaFieldKeys(strategy));
    const pushField = (key) => {
        if (isModeSuppressedStrategyField(key)) {
            return;
        }

        if (shouldSuppressLegacyStrategyField(strategy, key)) {
            return;
        }

        if (groups.some(([existingKey]) => existingKey === key)) {
            return;
        }

        const hasOwnValue = Object.prototype.hasOwnProperty.call(strategy, key);
        if (!hasOwnValue) {
            if (schemaKeys.has(key) || key === 'id' || key === 'dll' || key === 'instruments' || readonlyStrategyFieldKeys.has(key) || (key === 'backtest_data_dir' && state.mode === 'backtest')) {
                groups.push([key, formatStrategyFieldLabel(key)]);
            }
            return;
        }

        const value = strategy[key];
        if (!isRenderableStrategyField(key, value)) {
            return;
        }

        if (key === 'backtest_data_dir' && state.mode !== 'backtest') {
            return;
        }

        groups.push([key, formatStrategyFieldLabel(key)]);
    };

    preferredStrategyFieldOrder.forEach(pushField);
    Array.from(schemaKeys).forEach(pushField);
    Object.entries(strategy).forEach(([key, value]) => {
        if (!isRenderableStrategyField(key, value)) {
            return;
        }

        if (key === 'backtest_data_dir' && state.mode !== 'backtest') {
            return;
        }

        pushField(key);
    });

    return groups;
}

function buildStrategyParameterSummary(strategy) {
    const parameterEntries = getStrategyFieldGroups(strategy)
        .map(([key, label]) => [key, label, getStrategyFieldValue(strategy, key)])
        .filter(([key]) => !['id', 'dll', 'backtest_data_dir', '__runtimeStatus', '__runtimeError', 'instruments'].includes(key))
        .filter(([, , value]) => value !== undefined && value !== null && value !== '');

    if (parameterEntries.length === 0) {
        return 'No editable parameters';
    }

    const visibleEntries = parameterEntries
        .slice(0, 3)
        .map(([, label, value]) => `${label} ${value}`);
    const remaining = parameterEntries.length - visibleEntries.length;
    if (remaining > 0) {
        visibleEntries.push(`+${remaining} more`);
    }

    return visibleEntries.join(' · ');
}

function escapeHtml(value) {
    return String(value)
        .replaceAll('&', '&amp;')
        .replaceAll('<', '&lt;')
        .replaceAll('>', '&gt;')
        .replaceAll('"', '&quot;')
        .replaceAll("'", '&#39;');
}

function applyState(next) {
    syncStrategyFieldBaselines(next);
    applyLocalStrategiesDraft(next);
    applyLocalStrategyFieldEdits(next);
    applyLocalLiveInventoryDraft(next);
    applyLocalRecorderDraft(next);
    Object.keys(state).forEach((key) => delete state[key]);
    Object.assign(state, next);
    if (state.mode === 'live') {
        syncStrategyStatusesFromLiveRuntime(state.live_runtime);
    }
}

function localStrategyFieldEditKey(strategyId, fieldKey) {
    return `${strategyId}\x1f${fieldKey}`;
}

function getStrategyPresetSaveKey(accountId, strategyId) {
    return `${accountId}\x1f${strategyId}`;
}

function clearSavedStrategyPresetKeysForStrategy(strategyId) {
    if (!strategyId || uiState.savedStrategyPresetKeys.size === 0) {
        return;
    }

    Array.from(uiState.savedStrategyPresetKeys).forEach((key) => {
        if (key.endsWith(`\x1f${strategyId}`)) {
            uiState.savedStrategyPresetKeys.delete(key);
        }
    });
}

function markStrategyPresetSaved(saveKey) {
    if (!saveKey) {
        return;
    }

    uiState.savedStrategyPresetKeys.add(saveKey);
    uiState.lastSavedStrategyPresetKey = saveKey;
}

function hasSavedStrategyPresetForStrategy(strategyId) {
    if (!strategyId) {
        return false;
    }

    return Array.from(uiState.savedStrategyPresetKeys).some((key) => key.endsWith(`\x1f${strategyId}`))
        || String(uiState.lastSavedStrategyPresetKey || '').endsWith(`\x1f${strategyId}`);
}

function hasLocalStrategyFieldEditsForStrategy(strategyId) {
    pruneLocalStrategyFieldEditsForStrategy(strategyId);
    if (!strategyId || uiState.localStrategyFieldEdits.size === 0) {
        return false;
    }

    for (const edit of uiState.localStrategyFieldEdits.values()) {
        if (edit.strategyId === strategyId) {
            return true;
        }
    }
    return false;
}

function hasLocalStrategiesDraftForStrategy(strategyId) {
    return Boolean(strategyId)
        && Array.isArray(uiState.localStrategiesDraft)
        && uiState.localStrategiesDraft.some((strategy) => strategy.id === strategyId);
}

function hasUnsavedStrategyPresetChanges(strategyId) {
    if (hasSavedStrategyPresetForStrategy(strategyId)) {
        return false;
    }

    return hasLocalStrategyFieldEditsForStrategy(strategyId);
}

function normalizeStrategyFieldEditValue(value) {
    return String(value ?? '');
}

function canonicalizeFieldValue(value) {
    const str = String(value ?? '').trim();
    if (str === '') return '';
    if (/^[+-]?(?:\d+\.?\d*|\.\d+)$/.test(str)) {
        const num = Number(str);
        if (isFinite(num)) return String(num);
    }
    const lower = str.toLowerCase();
    if (lower === 'true') return 'true';
    if (lower === 'false') return 'false';
    return str;
}

function getForcedStrategyFieldValueForMode(mode, key) {
    if (mode !== 'live') {
        return null;
    }

    return Object.prototype.hasOwnProperty.call(liveForcedStrategyFieldValues, key)
        ? liveForcedStrategyFieldValues[key]
        : null;
}

function getStrategyFieldBaselineValue(strategy, key, mode = state.mode) {
    const forcedValue = getForcedStrategyFieldValueForMode(mode, key);
    if (forcedValue !== null) {
        return normalizeStrategyFieldEditValue(forcedValue);
    }

    if (Object.prototype.hasOwnProperty.call(strategy, key)) {
        return normalizeStrategyFieldEditValue(strategy[key]);
    }

    return normalizeStrategyFieldEditValue(getStrategySchemaDefaultValue(strategy?.dll, key));
}

function isStrategyFieldBaselineKey(key) {
    return Boolean(key)
        && key !== 'id'
        && key !== 'dll'
        && !hiddenStrategyFieldKeys.has(key)
        && !readonlyStrategyFieldKeys.has(key)
        && !String(key).startsWith('__');
}

function collectStrategyFieldBaselineKeys(strategy) {
    const keys = new Set(Object.keys(strategy ?? {}));
    getStrategySchemaFieldKeys(strategy).forEach((key) => keys.add(key));
    preferredStrategyFieldOrder.forEach((key) => keys.add(key));
    return Array.from(keys).filter(isStrategyFieldBaselineKey);
}

function syncStrategyFieldBaselines(targetState) {
    if (!targetState || !Array.isArray(targetState.strategies)) {
        return;
    }

    const mode = targetState.mode === 'live' ? 'live' : 'backtest';
    const currentKeys = new Set();
    targetState.strategies.forEach((strategy) => {
        if (!strategy?.id) {
            return;
        }

        collectStrategyFieldBaselineKeys(strategy).forEach((fieldKey) => {
            const editKey = localStrategyFieldEditKey(strategy.id, fieldKey);
            const baselineValue = getStrategyFieldBaselineValue(strategy, fieldKey, mode);
            currentKeys.add(editKey);
            uiState.strategyFieldBaselines.set(editKey, baselineValue);
            const edit = uiState.localStrategyFieldEdits.get(editKey);
            if (edit && canonicalizeFieldValue(edit.value) === canonicalizeFieldValue(baselineValue)) {
                uiState.localStrategyFieldEdits.delete(editKey);
            }
        });
    });

    Array.from(uiState.strategyFieldBaselines.keys()).forEach((key) => {
        if (!currentKeys.has(key)) {
            uiState.strategyFieldBaselines.delete(key);
        }
    });
}

function syncStrategyFieldBaselineForStrategy(strategy, mode = state.mode) {
    if (!strategy?.id) {
        return;
    }

    collectStrategyFieldBaselineKeys(strategy).forEach((fieldKey) => {
        const editKey = localStrategyFieldEditKey(strategy.id, fieldKey);
        uiState.strategyFieldBaselines.set(editKey, getStrategyFieldBaselineValue(strategy, fieldKey, mode));
    });
}

function promoteStrategyPresetDraftToBaseline(strategyId) {
    const strategy = state.strategies.find((candidate) => candidate.id === strategyId);
    if (!strategy) {
        return;
    }

    syncStrategyFieldBaselineForStrategy(strategy, state.mode);
    clearLocalStrategyFieldEdits(strategyId);
}

function pruneLocalStrategyFieldEditsForStrategy(strategyId) {
    if (!strategyId || uiState.localStrategyFieldEdits.size === 0) {
        return;
    }

    Array.from(uiState.localStrategyFieldEdits.entries()).forEach(([key, edit]) => {
        if (edit.strategyId !== strategyId) {
            return;
        }

        const baselineValue = uiState.strategyFieldBaselines.get(key);
        if (baselineValue !== undefined && canonicalizeFieldValue(edit.value) === canonicalizeFieldValue(baselineValue)) {
            uiState.localStrategyFieldEdits.delete(key);
        }
    });
}

function setLocalStrategyFieldEdit(strategyId, fieldKey, value) {
    if (!strategyId || !fieldKey) {
        return;
    }

    const editKey = localStrategyFieldEditKey(strategyId, fieldKey);
    const normalizedValue = normalizeStrategyFieldEditValue(value);
    const baselineValue = uiState.strategyFieldBaselines.get(editKey);
    let isDirtyEdit = true;
    if (baselineValue !== undefined && canonicalizeFieldValue(normalizedValue) === canonicalizeFieldValue(baselineValue)) {
        uiState.localStrategyFieldEdits.delete(editKey);
        isDirtyEdit = false;
    } else {
        uiState.localStrategyFieldEdits.set(editKey, {
            strategyId,
            fieldKey,
            value
        });
    }

    const activeStrategy = state.strategies.find((candidate) => candidate.id === strategyId);
    if (activeStrategy) {
        activeStrategy[fieldKey] = value;
        activeStrategy.__runtimeError = '';
        if (activeStrategy.__runtimeStatus === 'failed') {
            activeStrategy.__runtimeStatus = 'stopped';
        }
    }
    if (isDirtyEdit) {
        clearSavedStrategyPresetKeysForStrategy(strategyId);
        uiState.lastSavedStrategyPresetKey = '';
    }
}

function applyLocalStrategyFieldEdits(targetState) {
    if (!targetState || !Array.isArray(targetState.strategies) || uiState.localStrategyFieldEdits.size === 0) {
        return;
    }

    uiState.localStrategyFieldEdits.forEach((edit) => {
        const strategy = targetState.strategies.find((candidate) => candidate.id === edit.strategyId);
        if (!strategy) {
            return;
        }
        strategy[edit.fieldKey] = edit.value;
    });
}

function clearLocalStrategyFieldEdits(strategyId = '') {
    if (!strategyId) {
        uiState.localStrategyFieldEdits.clear();
        return;
    }

    Array.from(uiState.localStrategyFieldEdits.entries()).forEach(([key, edit]) => {
        if (edit.strategyId === strategyId) {
            uiState.localStrategyFieldEdits.delete(key);
        }
    });
}

function cloneStrategyDraft(strategies) {
    return Array.isArray(strategies) ? structuredClone(strategies) : [];
}

function setLocalStrategiesDraft(strategies = state.strategies) {
    uiState.localStrategiesDraft = cloneStrategyDraft(strategies);
    uiState.lastSavedStrategyPresetKey = '';
}

function clearLocalStrategiesDraft() {
    uiState.localStrategiesDraft = null;
}

function reloadDashboardAfterPresetSave(strategyId, saveKey) {
    try {
        window.sessionStorage.setItem('itrader:lastSavedStrategyPresetKey', saveKey);
    } catch (storageError) {
        // Best-effort only. The reload still clears local edit state.
    }

    const reloadUrl = new URL(window.location.href);
    reloadUrl.searchParams.set('_saved_preset', strategyId);
    reloadUrl.searchParams.set('_saved_at', `${Date.now()}`);
    window.location.replace(reloadUrl.toString());
}

function confirmStrategyPresetRemoval(strategy, accountId, willDeletePreset) {
    const action = willDeletePreset
        ? `delete preset "${strategy.id}"`
        : `remove preset "${strategy.id}" from account "${accountId}"`;
    const detail = willDeletePreset
        ? 'This removes the saved strategy definition from the current config draft.'
        : 'The preset will remain available for other accounts.';
    return window.confirm(`${action}?\n\n${detail}\n\nClick OK to continue.`);
}

function applyLocalStrategiesDraft(targetState) {
    if (!targetState || !Array.isArray(targetState.strategies) || uiState.localStrategiesDraft === null) {
        return;
    }

    const incomingById = new Map(targetState.strategies.map((strategy) => [strategy.id, strategy]));
    targetState.strategies = cloneStrategyDraft(uiState.localStrategiesDraft).map((strategy) => {
        const incoming = incomingById.get(strategy.id);
        if (incoming) {
            strategy.__runtimeStatus = incoming.__runtimeStatus ?? strategy.__runtimeStatus;
            strategy.__runtimeError = incoming.__runtimeError ?? strategy.__runtimeError;
            if (incoming.runtime_details && typeof incoming.runtime_details === 'object') {
                strategy.runtime_details = incoming.runtime_details;
            }
        }
        ensureStrategyUiState(strategy);
        normalizeStrategyAccounts(strategy);
        return strategy;
    });
}

function copyLiveInventoryAdjustment(adjustment, index = 0) {
    return normalizeLiveInventoryAdjustment(adjustment, index);
}

function cloneLiveInventoryAdjustments(adjustments) {
    return Array.isArray(adjustments)
        ? adjustments.map((adjustment, index) => copyLiveInventoryAdjustment(adjustment, index))
        : [];
}

function cloneLiveInventoryPersistedPositions(positions) {
    return Array.isArray(positions)
        ? positions.map((position) => normalizeLiveInventoryPersistedPosition(position))
        : [];
}

function setLocalLiveInventoryDraftAdjustments(adjustments) {
    uiState.localLiveInventoryDraft = cloneLiveInventoryAdjustments(adjustments);
    if (state.live_inventory && typeof state.live_inventory === 'object') {
        state.live_inventory.adjustments = cloneLiveInventoryAdjustments(uiState.localLiveInventoryDraft);
    }
}

function setLocalPersistedInventoryDraftPositions(positions) {
    uiState.localPersistedInventoryDraft = cloneLiveInventoryPersistedPositions(positions);
    if (state.live_inventory && typeof state.live_inventory === 'object') {
        state.live_inventory.persisted_positions = cloneLiveInventoryPersistedPositions(uiState.localPersistedInventoryDraft);
    }
}

function applyLocalLiveInventoryDraft(targetState) {
    if (!targetState || (uiState.localLiveInventoryDraft === null && uiState.localPersistedInventoryDraft === null)) {
        return;
    }

    targetState.live_inventory = normalizeLiveInventoryPayload(targetState.live_inventory ?? {});
    if (uiState.localLiveInventoryDraft !== null) {
        targetState.live_inventory.adjustments = cloneLiveInventoryAdjustments(uiState.localLiveInventoryDraft);
    }
    if (uiState.localPersistedInventoryDraft !== null) {
        targetState.live_inventory.persisted_positions = cloneLiveInventoryPersistedPositions(uiState.localPersistedInventoryDraft);
    }
}

function clearLocalLiveInventoryDraft() {
    uiState.localLiveInventoryDraft = null;
    uiState.localPersistedInventoryDraft = null;
}

function cloneRecorderDraft(recorder) {
    const raw = recorder && typeof recorder === 'object'
        ? structuredClone(recorder)
        : {};
    return {
        ...normalizeRecorderPayload(raw),
        ...raw
    };
}

function setLocalRecorderField(fieldKey, value) {
    if (!fieldKey) {
        return;
    }

    const draft = cloneRecorderDraft(uiState.localRecorderDraft ?? getRecorderState());
    draft[fieldKey] = String(value ?? '');
    if (fieldKey === 'front') {
        draft.md_front = draft.front;
    } else if (fieldKey === 'md_front') {
        draft.front = draft.md_front;
    }

    uiState.localRecorderDraft = cloneRecorderDraft(draft);
    state.recorder = cloneRecorderDraft(draft);
}

function applyLocalRecorderDraft(targetState) {
    if (!targetState || uiState.localRecorderDraft === null) {
        return;
    }

    targetState.recorder = {
        ...normalizeRecorderPayload(targetState.recorder ?? {}),
        ...cloneRecorderDraft(uiState.localRecorderDraft)
    };
}

function clearLocalRecorderDraft() {
    uiState.localRecorderDraft = null;
}

function applyModeButtons(mode) {
    document.querySelectorAll('#mode-switch .segmented__item').forEach((button) => {
        button.classList.toggle('segmented__item--active', button.dataset.mode === mode);
    });
}

function sortedSetValues(set) {
    return Array.from(set ?? []).sort();
}

function sortedMapEntries(map) {
    return Array.from(map ?? []).sort(([leftKey, leftValue], [rightKey, rightValue]) => {
        if (leftKey === rightKey) {
            return String(leftValue).localeCompare(String(rightValue));
        }
        return String(leftKey).localeCompare(String(rightKey));
    });
}

function stripStrategyWarningsForRenderKey(strategy) {
    const runtimeDetails = Object.fromEntries(
        Object.entries(strategy?.runtime_details ?? {}).map(([accountId, details]) => [
            accountId,
            {
                ...details,
                warnings: []
            }
        ])
    );

    return {
        ...strategy,
        runtime_details: runtimeDetails
    };
}

function stripLiveInventoryWarningsForRenderKey(inventory) {
    if (!inventory || typeof inventory !== 'object') {
        return inventory;
    }

    return {
        ...inventory,
        warnings: []
    };
}

function buildAccountsRenderKey() {
    return JSON.stringify({
        mode: state.mode,
        apiConnected: runtime.apiConnected,
        accounts: state.accounts,
        strategies: state.strategies.map(stripStrategyWarningsForRenderKey),
        live_runtime: state.mode === 'live' ? state.live_runtime : null,
        live_inventory: state.mode === 'live' ? stripLiveInventoryWarningsForRenderKey(state.live_inventory) : null,
        ui: {
            collapsedStrategies: sortedSetValues(uiState.collapsedStrategies),
            expandedAccountStrategies: sortedSetValues(uiState.expandedAccountStrategies),
            accountStrategyTabs: sortedMapEntries(uiState.accountStrategyTabs),
            accountSummaryTabs: sortedMapEntries(uiState.accountSummaryTabs),
            expandedLiveInventoryAccounts: sortedSetValues(uiState.expandedLiveInventoryAccounts),
            pendingStrategyBrowseAccount: uiState.pendingStrategyBrowseAccount,
            pendingBacktestRunKey: uiState.pendingBacktestRunKey,
            pendingStrategyPresetSaveKey: uiState.pendingStrategyPresetSaveKey,
            savedStrategyPresetKeys: sortedSetValues(uiState.savedStrategyPresetKeys),
            lastSavedStrategyPresetKey: uiState.lastSavedStrategyPresetKey,
            localStrategyFieldEditCount: uiState.localStrategyFieldEdits.size,
            localPersistedInventoryDraft: uiState.localPersistedInventoryDraft,
            strategyPresetSaveErrorCount: uiState.strategyPresetSaveErrors.size
        },
        runtime: {
            pendingBacktestDetailLevel: runtime.pendingBacktestDetailLevel,
            pendingLiveControlAction: runtime.pendingLiveControlAction,
            pendingLiveControlKey: runtime.pendingLiveControlKey,
            pendingLiveInventorySave: runtime.pendingLiveInventorySave,
            pendingLiveInventoryRefresh: runtime.pendingLiveInventoryRefresh
        }
    });
}

function collectRuntimeLogEntries() {
    if (state.mode !== 'live') {
        return [];
    }

    const entries = [];
    state.accounts.forEach((account) => {
        getAssignedStrategies(account.id).forEach((strategy) => {
            normalizeStrategyRuntimeDetails(strategy);
            const warnings = strategy.runtime_details?.[account.id]?.warnings ?? [];
            warnings.forEach((warningText) => {
                entries.push(`[${account.id}] ${strategy.id}: ${warningText}`);
            });
        });
    });

    const inventoryWarnings = getLiveInventoryState().warnings ?? [];
    inventoryWarnings.forEach((warningText) => {
        entries.push(`[inventory] ${warningText}`);
    });

    return uniqueValues(entries);
}

function buildRuntimeLogScopeKey() {
    return `${state.mode}|${runtime.sourceConfig || defaultConfigPathForMode(state.mode)}`;
}

function buildRuntimeLogRenderKey(entries) {
    return JSON.stringify({
        scope: buildRuntimeLogScopeKey(),
        entries
    });
}

function stabilizeRuntimeLogEntries(entries) {
    const currentEntries = uniqueValues(entries);
    const scopeKey = buildRuntimeLogScopeKey();
    const previousEntries = renderSectionCache.runtimeLogScopeKey === scopeKey
        && Array.isArray(renderSectionCache.runtimeLogEntries)
        ? renderSectionCache.runtimeLogEntries
        : [];

    if (previousEntries.length === 0 || currentEntries.length === 0) {
        return currentEntries.length > 0 ? currentEntries : previousEntries;
    }

    const mergedEntries = [...previousEntries];
    const previousEntrySet = new Set(previousEntries);
    currentEntries.forEach((entry) => {
        if (!previousEntrySet.has(entry)) {
            mergedEntries.push(entry);
            previousEntrySet.add(entry);
        }
    });

    return mergedEntries;
}

function alignRuntimeLogEntries(entries) {
    const previousEntries = Array.isArray(renderSectionCache.runtimeLogEntries)
        ? renderSectionCache.runtimeLogEntries
        : [];
    const previousIndexByEntry = new Map(previousEntries.map((entry, index) => [entry, index]));

    return [...entries].sort((leftEntry, rightEntry) => {
        const leftIndex = previousIndexByEntry.get(leftEntry);
        const rightIndex = previousIndexByEntry.get(rightEntry);
        const leftKnown = Number.isInteger(leftIndex);
        const rightKnown = Number.isInteger(rightIndex);

        if (leftKnown && rightKnown) {
            return leftIndex - rightIndex;
        }
        if (leftKnown) {
            return -1;
        }
        if (rightKnown) {
            return 1;
        }
        return 0;
    });
}

function ensureRuntimeLogPanelScaffold() {
    let summary = runtimeLogContent.querySelector('.runtime-log-summary');
    if (!summary) {
        summary = document.createElement('p');
        summary.className = 'runtime-log-summary';
        runtimeLogContent.append(summary);
    }

    let empty = runtimeLogContent.querySelector('.runtime-log-empty');
    if (!empty) {
        empty = document.createElement('div');
        empty.className = 'runtime-log-empty';
        empty.hidden = true;
        runtimeLogContent.append(empty);
    }

    let list = runtimeLogContent.querySelector('.runtime-log-list');
    if (!list) {
        list = document.createElement('div');
        list.className = 'runtime-log-list';
        runtimeLogContent.append(list);
    }

    return { summary, empty, list };
}

function syncRuntimeLogList(list, entries) {
    const nodeByEntry = new Map(
        Array.from(list.querySelectorAll('.runtime-log-entry')).map((node) => [node.dataset.entryText || node.textContent || '', node])
    );

    entries.forEach((entryText, index) => {
        let item = nodeByEntry.get(entryText);
        if (!item) {
            item = document.createElement('div');
            item.className = 'runtime-log-entry';
            item.dataset.entryText = entryText;
            item.textContent = entryText;
        } else if (item.textContent !== entryText) {
            item.dataset.entryText = entryText;
            item.textContent = entryText;
        }

        const currentNodeAtIndex = list.children[index];
        if (currentNodeAtIndex !== item) {
            list.insertBefore(item, currentNodeAtIndex || null);
        }

        nodeByEntry.delete(entryText);
    });

    nodeByEntry.forEach((node) => {
        node.remove();
    });
}

function renderRuntimeLogPanel() {
    if (!runtimeLogSection || !runtimeLogContent) {
        return;
    }

    const isLive = state.mode === 'live';
    runtimeLogSection.classList.toggle('hidden', !isLive);
    if (!isLive) {
        renderSectionCache.runtimeLogScopeKey = '';
        renderSectionCache.runtimeLogKey = '';
        renderSectionCache.runtimeLogEntries = [];
        runtimeLogContent.innerHTML = '';
        return;
    }

    const entries = alignRuntimeLogEntries(stabilizeRuntimeLogEntries(collectRuntimeLogEntries()));
    const nextRuntimeLogKey = buildRuntimeLogRenderKey(entries);
    if (renderSectionCache.runtimeLogKey === nextRuntimeLogKey && runtimeLogContent.childElementCount > 0) {
        return;
    }

    renderSectionCache.runtimeLogScopeKey = buildRuntimeLogScopeKey();
    renderSectionCache.runtimeLogKey = nextRuntimeLogKey;
    renderSectionCache.runtimeLogEntries = entries;

    const { summary, empty, list } = ensureRuntimeLogPanelScaffold();

    summary.textContent = entries.length > 0
        ? `${entries.length} unique live log line${entries.length === 1 ? '' : 's'} have been captured in this page session from runtime warnings and reconnect events.`
        : 'No live runtime warnings have been captured in this page session yet.';

    if (entries.length === 0) {
        empty.textContent = 'This panel stays empty until the live runtime reports warnings, reconnect notices, or similar operator-facing messages.';
        empty.hidden = false;
        list.hidden = true;
        list.replaceChildren();
        return;
    }

    empty.hidden = true;
    list.hidden = false;
    syncRuntimeLogList(list, entries);
}

function parseInstrumentList(value) {
    return String(value ?? '')
        .split(',')
        .map((part) => part.trim())
        .filter(Boolean);
}

function uniqueValues(values) {
    return Array.from(new Set(values.filter(Boolean)));
}

function sanitizeStrategyId(raw) {
    const collapsed = String(raw ?? '')
        .trim()
        .replace(/\.dll$/i, '')
        .replace(/[^A-Za-z0-9_]+/g, '_')
        .replace(/^_+|_+$/g, '');

    if (!collapsed) {
        return 'strategy';
    }

    return /^\d/.test(collapsed) ? `strategy_${collapsed}` : collapsed;
}

function strategyBaseName(filePath) {
    const parts = String(filePath ?? '').split(/[\\/]/);
    return parts.at(-1)?.replace(/\.dll$/i, '') || 'strategy';
}

function nextUniqueStrategyId(baseId) {
    const normalizedBase = sanitizeStrategyId(baseId);
    let candidate = normalizedBase;
    let suffix = 2;

    while (state.strategies.some((strategy) => strategy.id === candidate)) {
        candidate = `${normalizedBase}_${suffix}`;
        suffix += 1;
    }

    return candidate;
}

function resetStrategyFileCatalog() {
    runtime.strategyFileCatalog = [];
    runtime.strategyFileCatalogRoot = '../strategies/bin';
    runtime.strategyFileCatalogKey = '';
    runtime.strategyFileCatalogStatus = 'idle';
    runtime.strategyFileCatalogError = '';
}

function currentStrategyFileCatalogKey() {
    return `${state.mode}:${effectiveRequestedConfig(state.mode)}`;
}

async function loadStrategyFileCatalog({ force = false, silent = false } = {}) {
    const requestKey = currentStrategyFileCatalogKey();
    if (!force && runtime.strategyFileCatalogStatus === 'ready' && runtime.strategyFileCatalogKey === requestKey) {
        normalizeStateStrategyDllPaths();
        return runtime.strategyFileCatalog;
    }

    runtime.strategyFileCatalogStatus = 'loading';
    runtime.strategyFileCatalogError = '';

    try {
        const query = buildModeQuery(state.mode);
        const response = await fetch(`${API_BASE}/api/strategy-files?${query.toString()}`);
        if (!response.ok) {
            throw new Error(`HTTP ${response.status}`);
        }

        const payload = await response.json();
        runtime.strategyFileCatalog = Array.isArray(payload.strategy_files) ? payload.strategy_files : [];
        runtime.strategyFileCatalogRoot = payload.catalog_root || '../strategies/bin';
        runtime.strategyFileCatalogKey = requestKey;
        runtime.strategyFileCatalogStatus = 'ready';
        runtime.strategyFileCatalogError = '';
        const { normalizedStrategyCount, removedLegacyFieldCount } = normalizeStateStrategyDllPaths();
        if (!silent) {
            if (normalizedStrategyCount > 0 || removedLegacyFieldCount > 0) {
                const catalogActions = [];
                if (normalizedStrategyCount > 0) {
                    catalogActions.push(`normalized ${normalizedStrategyCount} saved strategy definition${normalizedStrategyCount === 1 ? '' : 's'} to catalog remote paths`);
                }
                if (removedLegacyFieldCount > 0) {
                    catalogActions.push(`removed ${removedLegacyFieldCount} legacy non-schema parameter${removedLegacyFieldCount === 1 ? '' : 's'}`);
                }
                runtime.lastMessage = `Loaded ${runtime.strategyFileCatalog.length} strategy DLLs from ${runtime.strategyFileCatalogRoot} and ${catalogActions.join(' and ')}.`;
            } else {
                runtime.lastMessage = `Loaded ${runtime.strategyFileCatalog.length} strategy DLLs from ${runtime.strategyFileCatalogRoot}.`;
            }
        }
        return runtime.strategyFileCatalog;
    } catch (error) {
        runtime.strategyFileCatalog = [];
        runtime.strategyFileCatalogRoot = '../strategies/bin';
        runtime.strategyFileCatalogKey = requestKey;
        runtime.strategyFileCatalogStatus = 'error';
        runtime.strategyFileCatalogError = error instanceof Error ? error.message : String(error);
        if (!silent) {
            runtime.lastMessage = `Unable to load strategy DLLs from ${runtime.strategyFileCatalogRoot}: ${runtime.strategyFileCatalogError}.`;
        }
        return [];
    }
}

function catalogHasStrategyFilename(filename) {
    const target = String(filename ?? '').trim().toLowerCase();
    if (!target) {
        return false;
    }
    return runtime.strategyFileCatalog.some((entry) => (
        strategyDllFileName(entry?.filename || entry?.dll || entry?.absolute_path) === target
    ));
}

async function uploadStrategyDllFile(file, { overwrite = false } = {}) {
    if (!file || runtime.pendingStrategyUpload) {
        return;
    }

    const filename = String(file.name || '').trim();
    if (!filename.toLowerCase().endsWith('.dll')) {
        runtime.lastMessage = 'Choose a .dll strategy file before uploading.';
        render();
        return;
    }

    if (!overwrite && catalogHasStrategyFilename(filename)) {
        const confirmed = window.confirm(`Strategy DLL ${filename} already exists in ${runtime.strategyFileCatalogRoot}. Overwrite it? Stop any running strategy that uses this DLL before continuing.`);
        if (!confirmed) {
            runtime.lastMessage = `Upload cancelled for ${filename}.`;
            render();
            return;
        }
        await uploadStrategyDllFile(file, { overwrite: true });
        return;
    }

    const query = buildModeQuery(state.mode);
    if (overwrite) {
        query.set('overwrite', 'true');
    }

    runtime.pendingStrategyUpload = true;
    runtime.lastMessage = `Uploading ${filename} to ${runtime.strategyFileCatalogRoot}...`;
    render();

    try {
        const response = await fetch(`${API_BASE}/api/strategy-files/upload?${query.toString()}`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/octet-stream',
                'X-Strategy-Filename': encodeURIComponent(filename)
            },
            body: file
        });
        const payload = await response.json().catch(() => ({}));
        if (!response.ok) {
            throw new Error(payload.message || `HTTP ${response.status}`);
        }

        if (payload.conflict && !overwrite) {
            const confirmed = window.confirm(`${payload.message || `Strategy DLL ${filename} already exists.`} Overwrite it?`);
            if (confirmed) {
                runtime.pendingStrategyUpload = false;
                await uploadStrategyDllFile(file, { overwrite: true });
                return;
            }
            runtime.lastMessage = `Upload cancelled for ${filename}.`;
            return;
        }

        if (!payload.ok) {
            runtime.lastMessage = payload.message || `Upload rejected for ${filename}.`;
            return;
        }

        runtime.lastMessage = payload.message || `Uploaded ${filename}.`;
        await loadStrategyFileCatalog({ force: true, silent: true });
    } catch (error) {
        const detail = error instanceof Error ? error.message : String(error);
        runtime.lastMessage = `Strategy DLL upload failed for ${filename}: ${detail}`;
    } finally {
        runtime.pendingStrategyUpload = false;
        if (strategyUploadInput) {
            strategyUploadInput.value = '';
        }
        render();
    }
}

async function browseBacktestDirectory() {
    if (runtime.pendingBacktestDirectoryBrowse) {
        return;
    }

    runtime.pendingBacktestDirectoryBrowse = true;
    runtime.lastMessage = 'Opening a native folder picker for the backtest data directory...';
    render();

    try {
        const query = buildModeQuery('backtest');
        const currentDirectory = String(state.backtest.data_dir ?? '').trim();
        if (currentDirectory) {
            query.set('current', currentDirectory);
        }

        const response = await fetch(`${API_BASE}/api/pick-backtest-directory?${query.toString()}`);
        if (!response.ok) {
            throw new Error(`HTTP ${response.status}`);
        }

        const payload = await response.json();
        if (payload.cancelled) {
            runtime.lastMessage = 'Backtest data directory selection was cancelled.';
            return;
        }

        state.backtest.data_dir = payload.directory || payload.absolute_path || state.backtest.data_dir;
        runtime.apiConnected = true;
        if (payload.config_path) {
            runtime.sourceConfig = payload.config_path;
        }
        runtime.lastMessage = `Selected backtest data directory: ${state.backtest.data_dir}`;
    } catch (error) {
        const detail = error instanceof Error ? error.message : String(error);
        runtime.lastMessage = `Unable to open the backtest data directory picker. On remote deployments, this picker needs an interactive desktop on the UI API host, so it is often unavailable. Enter a server-side path manually instead (for example ../runtime/ctp_md_recorder/agtick). (${detail})`;
    } finally {
        runtime.pendingBacktestDirectoryBrowse = false;
        render();
    }
}

async function fetchModeStatePayload(mode, { replayBacktest = false, signal = undefined } = {}) {
    const query = buildModeQuery(mode);
    if (mode === 'backtest' && replayBacktest) {
        query.set('replay', '1');
    }
    const response = await fetch(`${API_BASE}/api/state?${query.toString()}`, signal ? { signal } : undefined);
    if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
    }
    return response.json();
}

function parseChartTimestamp(value) {
    if (typeof value === 'number' && Number.isFinite(value)) {
        return Math.trunc(value);
    }

    if (typeof value === 'string' && value.trim()) {
        const normalized = value.trim().replace(' ', 'T');
        const epochMs = Date.parse(normalized);
        if (Number.isFinite(epochMs)) {
            return Math.trunc(epochMs / 1000);
        }
    }

    return null;
}

function formatMarkerText(order) {
    const side = String(order.side ?? '').toLowerCase() === 'sell' ? 'Sell' : 'Buy';
    const offset = order.offset ? ` ${order.offset}` : '';
    const price = order.filled_price ?? order.limit_price;
    const detail = price !== undefined && price !== null && price !== ''
        ? ` @ ${formatDisplayNumber(price)}`
        : '';
    return `${order.strategy_id || 'Strategy'} ${side}${offset}${detail}`;
}

function isDryRunBlockedOrder(order) {
    return String(order.status ?? '').toLowerCase() === 'rejected'
        && Number(order.filled_volume ?? 0) === 0
        && String(order.message ?? '').includes('Dry run blocked live order')
        && Number(order.limit_price ?? 0) > 0;
}

function shouldEmitFallbackSignal(order) {
    return Number(order.filled_volume ?? 0) > 0 || isDryRunBlockedOrder(order);
}

function collectFallbackSignals(sourceState, instrument, bars) {
    const details = sourceState.strategies.flatMap((strategy) =>
        Object.entries(strategy.runtime_details ?? {}).flatMap(([accountId, runtimeDetails]) =>
            (runtimeDetails.closed_orders ?? []).map((order) => ({
                ...order,
                strategy_id: order.strategy_id || strategy.id,
                account_id: order.account_id || accountId
            }))
        )
    );

    const fallbackTime = bars.at(-1)?.time ?? Math.trunc(Date.now() / 1000);
    return details
        .filter((order) => !order.instrument || order.instrument === instrument)
        .filter((order) => shouldEmitFallbackSignal(order))
        .map((order) => {
            const side = String(order.side ?? '').toLowerCase() === 'sell' ? 'sell' : 'buy';
            return {
                time: parseChartTimestamp(order.timestamp) ?? fallbackTime,
                price: Number(order.filled_price ?? order.limit_price ?? 0),
                position: side === 'sell' ? 'aboveBar' : 'belowBar',
                color: side === 'sell' ? '#fb7185' : '#34d399',
                shape: side === 'sell' ? 'arrowDown' : 'arrowUp',
                text: formatMarkerText(order),
                strategy_id: order.strategy_id || '',
                account_id: order.account_id || ''
            };
        })
        .sort((left, right) => left.time - right.time);
}

function buildFallbackBars(points, instrumentIndex) {
    const start = Math.trunc(Date.now() / 1000) - (points.length - 1) * 60;
    const instrumentNudge = instrumentIndex * 1.25;
    return points.map((close, index) => {
        const adjustedClose = close + instrumentNudge;
        const previousBase = index === 0 ? close : points[index - 1];
        const previous = previousBase + instrumentNudge;
        const spread = Math.max(Math.abs(adjustedClose - previous) * 0.25, 1);
        return {
            time: start + index * 60,
            open: previous,
            high: Math.max(previous, adjustedClose) + spread,
            low: Math.min(previous, adjustedClose) - spread,
            close: adjustedClose
        };
    });
}

function createFallbackChartPayload(sourceState = state) {
    const instruments = uniqueValues(sourceState.strategies.flatMap((strategy) => parseInstrumentList(strategy.instruments)));
    const instrumentList = instruments.length > 0 ? instruments : ['IF2506'];
    const accountIds = uniqueValues(sourceState.accounts.map((account) => account.id));
    const points = Array.isArray(sourceState.equity) && sourceState.equity.length > 1
        ? sourceState.equity
        : [100, 102, 101, 104, 107, 105];

    const instrumentEntries = instrumentList.map((instrument, index) => {
        const bars = buildFallbackBars(points, index);
        return {
            instrument,
            bars,
            indicator_series: [],
            signals: collectFallbackSignals(sourceState, instrument, bars),
            warnings: []
        };
    });

    const primaryInstrument = instrumentEntries[0] ?? {
        instrument: 'IF2506',
        bars: [],
        indicator_series: [],
        signals: [],
        warnings: []
    };

    return {
        instrument: primaryInstrument.instrument,
        default_instrument: primaryInstrument.instrument,
        source: 'sample-fallback',
        account_ids: accountIds,
        bars: primaryInstrument.bars,
        indicator_series: primaryInstrument.indicator_series,
        signals: primaryInstrument.signals,
        instruments: instrumentEntries,
        warnings: runtime.apiConnected
            ? ['Chart feed is not available in the API response yet, so the dashboard is showing a synthetic preview.']
            : ['Local API is offline, so the dashboard is showing a synthetic preview chart.']
    };
}

function normalizeChartInstrumentEntry(entry) {
    const bars = Array.isArray(entry?.bars)
        ? entry.bars
            .map((bar) => ({
                time: parseChartTimestamp(bar.time),
                open: Number(bar.open),
                high: Number(bar.high),
                low: Number(bar.low),
                close: Number(bar.close)
            }))
            .filter((bar) => bar.time !== null && [bar.open, bar.high, bar.low, bar.close].every(Number.isFinite))
            .sort((left, right) => left.time - right.time)
        : [];

    const signals = Array.isArray(entry?.signals)
        ? entry.signals
            .map((signal) => ({
                time: parseChartTimestamp(signal.time),
                price: Number(signal.price ?? 0),
                position: signal.position || 'belowBar',
                color: signal.color || '#34d399',
                shape: signal.shape || 'arrowUp',
                text: signal.text || '',
                strategy_id: signal.strategy_id || '',
                account_id: signal.account_id || ''
            }))
            .filter((signal) => signal.time !== null)
            .sort((left, right) => left.time - right.time)
        : [];

    const indicatorSeries = Array.isArray(entry?.indicator_series)
        ? entry.indicator_series
            .map((series) => ({
                indicator_id: String(series?.indicator_id ?? '').trim(),
                label: String(series?.label ?? series?.indicator_id ?? '').trim(),
                color: String(series?.color ?? '').trim(),
                strategy_id: String(series?.strategy_id ?? '').trim(),
                account_id: String(series?.account_id ?? '').trim(),
                points: Array.isArray(series?.points)
                    ? series.points
                        .map((point) => ({
                            time: parseChartTimestamp(point.time),
                            value: Number(point.value)
                        }))
                        .filter((point) => point.time !== null && Number.isFinite(point.value))
                        .sort((left, right) => left.time - right.time)
                    : []
            }))
            .filter((series) => series.indicator_id && series.points.length > 0)
        : [];

    return {
        instrument: entry?.instrument || '',
        bars,
        indicator_series: indicatorSeries,
        signals,
        warnings: Array.isArray(entry?.warnings) ? entry.warnings : []
    };
}

function cloneChartInstrumentEntry(entry) {
    return {
        instrument: entry.instrument,
        bars: entry.bars.map((bar) => ({ ...bar })),
        indicator_series: entry.indicator_series.map((series) => ({
            ...series,
            points: Array.isArray(series.points) ? series.points.map((point) => ({ ...point })) : []
        })),
        signals: entry.signals.map((signal) => ({ ...signal })),
        warnings: Array.isArray(entry.warnings) ? [...entry.warnings] : []
    };
}

function mergeLiveChartBars(cachedBars, incomingBars, maxBars = 0) {
    const mergedByTime = new Map();
    const addBar = (bar) => {
        if (!bar || bar.time === null || bar.time === undefined) {
            return;
        }
        mergedByTime.set(bar.time, { ...bar });
    };

    (Array.isArray(cachedBars) ? cachedBars : []).forEach(addBar);
    (Array.isArray(incomingBars) ? incomingBars : []).forEach(addBar);

    const merged = Array.from(mergedByTime.values())
        .sort((left, right) => left.time - right.time);
    if (maxBars > 0 && merged.length > maxBars) {
        return merged.slice(merged.length - maxBars);
    }
    return merged;
}

function chartIndicatorSeriesKey(series) {
    return `${series.strategy_id || ''}:${series.account_id || ''}:${series.indicator_id || series.label || ''}`;
}

function latestChartPointTime(points) {
    return Array.isArray(points) && points.length > 0 ? (points.at(-1)?.time ?? 0) : 0;
}

function latestIndicatorSeriesTime(seriesList) {
    return (seriesList || []).reduce((latest, series) => Math.max(latest, latestChartPointTime(series.points)), 0);
}

function stabilizeIndicatorSeries(currentSeries, cachedSeries, latestNewBarTime, latestCachedBarTime) {
    if (!Array.isArray(cachedSeries) || cachedSeries.length === 0) {
        return Array.isArray(currentSeries) ? currentSeries : [];
    }

    if (!Array.isArray(currentSeries) || currentSeries.length === 0) {
        return cachedSeries;
    }

    const cachedByKey = new Map(cachedSeries.map((series) => [chartIndicatorSeriesKey(series), series]));
    const currentByKey = new Map(currentSeries.map((series) => [chartIndicatorSeriesKey(series), series]));
    const currentLatestIndicatorTime = latestIndicatorSeriesTime(currentSeries);
    const cachedLatestIndicatorTime = latestIndicatorSeriesTime(cachedSeries);
    let usedCachedSnapshot = false;

    const merged = cachedSeries.map((cachedSeriesEntry) => {
        const seriesKey = chartIndicatorSeriesKey(cachedSeriesEntry);
        const currentSeriesEntry = currentByKey.get(seriesKey);
        if (!currentSeriesEntry) {
            usedCachedSnapshot = true;
            return cachedSeriesEntry;
        }

        const currentPointCount = Array.isArray(currentSeriesEntry.points) ? currentSeriesEntry.points.length : 0;
        const cachedPointCount = Array.isArray(cachedSeriesEntry.points) ? cachedSeriesEntry.points.length : 0;
        const severePointShrinkThreshold = Math.max(3, Math.floor(cachedPointCount * 0.35));
        const currentLatestPointTime = latestChartPointTime(currentSeriesEntry.points);
        const cachedLatestPointTime = latestChartPointTime(cachedSeriesEntry.points);
        const severePointShrink = cachedPointCount >= 20
            && currentPointCount < severePointShrinkThreshold
            && currentLatestPointTime <= cachedLatestPointTime;

        if (severePointShrink) {
            usedCachedSnapshot = true;
            return cachedSeriesEntry;
        }

        return currentSeriesEntry;
    });

    currentSeries.forEach((series) => {
        const seriesKey = chartIndicatorSeriesKey(series);
        if (!cachedByKey.has(seriesKey)) {
            merged.push(series);
        }
    });

    const missingCachedSeries = cachedSeries.length > currentSeries.length;
    const likelyPartialIndicatorSnapshot = missingCachedSeries
        && (currentLatestIndicatorTime <= cachedLatestIndicatorTime || latestNewBarTime <= latestCachedBarTime);

    return usedCachedSnapshot || likelyPartialIndicatorSnapshot ? merged : currentSeries;
}

function stableChartCacheKey(instrument) {
    return `${state.mode}|${runtime.sourceConfig || ''}|${instrument}`;
}

function stabilizeLiveInstrumentEntries(entries) {
    if (state.mode !== 'live' || !runtime.apiConnected) {
        return entries;
    }

    return entries.map((entry) => {
        if (!entry.instrument) {
            return entry;
        }

        const cacheKey = stableChartCacheKey(entry.instrument);
        const cached = chartRuntime.stableInstrumentCache.get(cacheKey);
        if (!Array.isArray(entry.bars) || entry.bars.length === 0) {
            if (cached && Array.isArray(cached.bars) && cached.bars.length > 0) {
                return {
                    ...entry,
                    bars: cached.bars,
                    indicator_series: cached.indicator_series.length > 0 ? cached.indicator_series : entry.indicator_series,
                    signals: cached.signals.length > 0 ? cached.signals : entry.signals,
                    warnings: entry.warnings.length > 0 ? entry.warnings : cached.warnings
                };
            }
            return entry;
        }

        if (!cached || !Array.isArray(cached.bars) || cached.bars.length === 0) {
            chartRuntime.stableInstrumentCache.set(cacheKey, cloneChartInstrumentEntry(entry));
            return entry;
        }

        const latestNew = entry.bars.at(-1)?.time ?? 0;
        const latestCached = cached.bars.at(-1)?.time ?? 0;
        const severeShrinkThreshold = Math.max(3, Math.floor(cached.bars.length * 0.35));
        const severeShrink = cached.bars.length >= 20 && entry.bars.length < severeShrinkThreshold;
        const looksLikePartialSnapshot = severeShrink;
        const mergedBars = looksLikePartialSnapshot
            ? mergeLiveChartBars(cached.bars, entry.bars, Math.max(cached.bars.length, entry.bars.length))
            : entry.bars;
        const latestMerged = mergedBars.at(-1)?.time ?? latestNew;
        const stableIndicatorSeries = stabilizeIndicatorSeries(entry.indicator_series, cached.indicator_series, latestMerged, latestCached);
        const stabilizedEntry = {
            ...entry,
            bars: mergedBars,
            indicator_series: stableIndicatorSeries,
            signals: entry.signals.length > 0 ? entry.signals : cached.signals,
            warnings: entry.warnings.length > 0 ? entry.warnings : cached.warnings
        };

        if (looksLikePartialSnapshot) {
            chartRuntime.stableInstrumentCache.set(cacheKey, cloneChartInstrumentEntry(stabilizedEntry));
            return stabilizedEntry;
        }

        const shouldRefreshCache = mergedBars.length >= Math.floor(cached.bars.length * 0.6)
            || latestMerged > latestCached;
        if (shouldRefreshCache) {
            chartRuntime.stableInstrumentCache.set(cacheKey, cloneChartInstrumentEntry(stabilizedEntry));
        }
        return stabilizedEntry;
    });
}

function normalizeChartPayload(chart, sourceState = state) {
    const fallback = createFallbackChartPayload(sourceState);
    const hasApiChart = Boolean(chart) && (
        Array.isArray(chart?.instruments)
        || Array.isArray(chart?.bars)
        || Array.isArray(chart?.indicator_series)
        || Array.isArray(chart?.signals)
        || Boolean(chart?.instrument)
        || Boolean(chart?.source)
    );

    const normalizedInstruments = Array.isArray(chart?.instruments) && chart.instruments.length > 0
        ? chart.instruments
            .map((entry) => normalizeChartInstrumentEntry(entry))
            .filter((entry) => entry.instrument)
        : (() => {
            if (!chart?.instrument && !Array.isArray(chart?.bars) && !Array.isArray(chart?.signals) && !Array.isArray(chart?.indicator_series)) {
                return fallback.instruments;
            }

            return [normalizeChartInstrumentEntry({
                instrument: chart?.instrument || fallback.default_instrument,
                bars: chart?.bars,
                indicator_series: chart?.indicator_series,
                signals: chart?.signals,
                warnings: chart?.warnings
            })];
        })();

    const hasConcreteBars = normalizedInstruments.some((entry) => entry.bars.length > 0);
    const shouldAvoidFallbackBars = state.mode === 'live' && hasApiChart;
    let instrumentEntries = hasConcreteBars
        ? normalizedInstruments
        : (shouldAvoidFallbackBars
            ? normalizedInstruments
            : fallback.instruments.map((fallbackEntry) => {
                const apiEntry = normalizedInstruments.find((entry) => entry.instrument === fallbackEntry.instrument);
                if (!apiEntry) {
                    return fallbackEntry;
                }

                return {
                    instrument: fallbackEntry.instrument,
                    bars: fallbackEntry.bars,
                    indicator_series: apiEntry.indicator_series.length > 0 ? apiEntry.indicator_series : fallbackEntry.indicator_series,
                    signals: apiEntry.signals.length > 0 ? apiEntry.signals : fallbackEntry.signals,
                    warnings: apiEntry.warnings.length > 0 ? apiEntry.warnings : fallbackEntry.warnings
                };
            }));

    instrumentEntries = stabilizeLiveInstrumentEntries(instrumentEntries);

    const instrumentNames = instrumentEntries.map((entry) => entry.instrument);
    const defaultInstrument = instrumentNames.includes(chart?.default_instrument)
        ? chart.default_instrument
        : (instrumentNames.includes(chart?.instrument) ? chart.instrument : (instrumentNames[0] || fallback.default_instrument));
    const primaryInstrument = instrumentEntries.find((entry) => entry.instrument === defaultInstrument) ?? instrumentEntries[0] ?? fallback.instruments[0];
    const accountIds = uniqueValues([
        ...(Array.isArray(chart?.account_ids) ? chart.account_ids : []),
        ...sourceState.accounts.map((account) => account.id),
        ...instrumentEntries.flatMap((entry) => entry.indicator_series.map((series) => series.account_id)),
        ...instrumentEntries.flatMap((entry) => entry.signals.map((signal) => signal.account_id))
    ]);

    return {
        instrument: primaryInstrument?.instrument || fallback.instrument,
        default_instrument: defaultInstrument,
        source: chart?.source || fallback.source,
        account_ids: accountIds.length > 0 ? accountIds : fallback.account_ids,
        bars: primaryInstrument?.bars?.length > 0 ? primaryInstrument.bars : fallback.bars,
        indicator_series: primaryInstrument?.indicator_series ?? fallback.indicator_series,
        signals: primaryInstrument?.signals ?? fallback.signals,
        instruments: instrumentEntries,
        warnings: Array.isArray(chart?.warnings)
            ? (chart.warnings.length > 0 ? chart.warnings : (hasApiChart ? [] : fallback.warnings))
            : fallback.warnings
    };
}

function getActiveChartInstrumentEntry(chart) {
    const available = chart.instruments.map((entry) => entry.instrument);
    if (available.length === 0) {
        uiState.chartInstrument = '';
        return null;
    }

    if (!uiState.chartInstrument || !available.includes(uiState.chartInstrument)) {
        uiState.chartInstrument = available.includes(chart.default_instrument) ? chart.default_instrument : available[0];
    }

    return chart.instruments.find((entry) => entry.instrument === uiState.chartInstrument) ?? chart.instruments[0];
}

function getActiveChartAccount(chart) {
    const options = ['all', ...chart.account_ids];
    if (!options.includes(uiState.chartAccount)) {
        uiState.chartAccount = 'all';
    }

    return uiState.chartAccount;
}

function getChartStrategyOptions(instrument, accountId, signals, indicatorSeries = []) {
    const fromSignals = signals.map((signal) => signal.strategy_id).filter(Boolean);
    const fromIndicators = indicatorSeries.map((series) => series.strategy_id).filter(Boolean);
    const fromConfig = state.strategies
        .filter((strategy) => parseInstrumentList(strategy.instruments).includes(instrument))
        .filter((strategy) => accountId === 'all' || strategy.accounts.includes(accountId))
        .map((strategy) => strategy.id);

    return uniqueValues([...fromSignals, ...fromIndicators, ...fromConfig]);
}

function getActiveChartStrategy(instrument, accountId, signals, indicatorSeries = []) {
    const options = getChartStrategyOptions(instrument, accountId, signals, indicatorSeries);
    if (options.length === 0) {
        uiState.chartStrategy = '';
        return '';
    }

    if (!options.includes(uiState.chartStrategy)) {
        uiState.chartStrategy = options[0];
    }

    return uiState.chartStrategy;
}

function filterChartSignals(signals, accountId, strategyId) {
    return signals.filter((signal) => {
        const accountMatch = accountId === 'all' || signal.account_id === accountId;
        const strategyMatch = !strategyId || signal.strategy_id === strategyId;
        return accountMatch && strategyMatch;
    });
}

function estimateChartBarStepSeconds(bars) {
    if (!Array.isArray(bars) || bars.length < 2) {
        return 60;
    }

    const steps = [];
    for (let index = 1; index < bars.length; index += 1) {
        const step = Number(bars[index]?.time ?? 0) - Number(bars[index - 1]?.time ?? 0);
        if (Number.isFinite(step) && step > 0) {
            steps.push(step);
        }
    }

    if (steps.length === 0) {
        return 60;
    }

    steps.sort((left, right) => left - right);
    return steps[Math.floor(steps.length / 2)] || 60;
}

function nearestChartBarTime(time, barTimes, toleranceSeconds) {
    if (!Array.isArray(barTimes) || barTimes.length === 0 || !Number.isFinite(time)) {
        return null;
    }

    let low = 0;
    let high = barTimes.length - 1;
    while (low <= high) {
        const middle = Math.floor((low + high) / 2);
        const middleTime = barTimes[middle];
        if (middleTime === time) {
            return middleTime;
        }
        if (middleTime < time) {
            low = middle + 1;
        } else {
            high = middle - 1;
        }
    }

    const candidates = [];
    if (high >= 0) {
        candidates.push(barTimes[high]);
    }
    if (low < barTimes.length) {
        candidates.push(barTimes[low]);
    }

    let bestTime = null;
    let bestDistance = Infinity;
    candidates.forEach((candidate) => {
        const distance = Math.abs(candidate - time);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestTime = candidate;
        }
    });

    return bestDistance <= toleranceSeconds ? bestTime : null;
}

function alignSignalsToChartBars(signals, bars) {
    if (!Array.isArray(signals) || signals.length === 0 || !Array.isArray(bars) || bars.length === 0) {
        return [];
    }

    const barTimes = bars.map((bar) => Number(bar.time)).filter(Number.isFinite);
    if (barTimes.length === 0) {
        return [];
    }

    const firstTime = barTimes[0];
    const lastTime = barTimes[barTimes.length - 1];
    const barStepSeconds = estimateChartBarStepSeconds(bars);
    const toleranceSeconds = Math.max(2, Math.floor(barStepSeconds / 2));

    return signals
        .map((signal) => {
            const signalTime = Number(signal.time);
            if (!Number.isFinite(signalTime) || signalTime < firstTime || signalTime > lastTime) {
                return null;
            }

            const alignedTime = nearestChartBarTime(signalTime, barTimes, toleranceSeconds);
            if (alignedTime === null) {
                return null;
            }

            return {
                ...signal,
                time: alignedTime
            };
        })
        .filter(Boolean);
}

function filterChartIndicatorSeries(indicatorSeries, accountId, strategyId) {
    return indicatorSeries.filter((series) => {
        const accountMatch = accountId === 'all' || !series.account_id || series.account_id === accountId;
        const strategyMatch = !strategyId || !series.strategy_id || series.strategy_id === strategyId;
        return accountMatch && strategyMatch;
    });
}

function renderChartToolbar(chart, activeInstrument, activeAccount) {
    chartInstrumentTabs.innerHTML = '';
    chart.instruments.forEach((entry) => {
        const button = document.createElement('button');
        const isActive = entry.instrument === activeInstrument;
        button.type = 'button';
        button.className = `chart-tab${isActive ? ' chart-tab--active' : ''}`;
        button.textContent = entry.instrument;
        button.setAttribute('role', 'tab');
        button.setAttribute('aria-selected', isActive ? 'true' : 'false');
        button.addEventListener('click', () => {
            uiState.chartInstrument = entry.instrument;
            render();
        });
        chartInstrumentTabs.append(button);
    });

    const options = ['all', ...chart.account_ids];
    chartAccountFilter.innerHTML = '';
    options.forEach((accountId) => {
        const option = document.createElement('option');
        option.value = accountId;
        option.textContent = accountId === 'all' ? 'All accounts' : accountId;
        option.selected = accountId === activeAccount;
        chartAccountFilter.append(option);
    });

    chartAccountFilter.oninput = (event) => {
        uiState.chartAccount = event.target.value;
        uiState.chartStrategy = 'all';
        render();
    };
}

function renderStrategyTabs(strategyIds, activeStrategy) {
    chartStrategyTabs.innerHTML = '';

    strategyIds.forEach((strategyId) => {
        const button = document.createElement('button');
        const isActive = strategyId === activeStrategy;
        button.type = 'button';
        button.className = `chart-tab${isActive ? ' chart-tab--active' : ''}`;
        button.textContent = strategyId;
        button.setAttribute('role', 'tab');
        button.setAttribute('aria-selected', isActive ? 'true' : 'false');
        button.addEventListener('click', () => {
            uiState.chartStrategy = strategyId;
            render();
        });
        chartStrategyTabs.append(button);
    });
}

function destroyChart() {
    resetChartMarkerRedrawSubscription();
    clearTradeMarkerLayer();
    if (chartRuntime.instance) {
        chartRuntime.instance.remove();
    }
    chartRuntime.instance = null;
    chartRuntime.candleSeries = null;
    chartRuntime.indicatorSeries = [];
    chartRuntime.lastRenderKey = '';
    chartRuntime.lastSeriesIdentityKey = '';
    chartRuntime.lastBarData = [];
    chartRuntime.lastIndicatorData.clear();
    chartRuntime.lastMarkerKey = '';
    chartRuntime.lastOverlayMarkerKey = '';
    chartRuntime.currentMarkerPayload = [];
    chartRuntime.hasFittedContent = false;
    chartRuntime.lastWidth = 0;
    chartRuntime.lastHeight = 0;
}

function buildChartRenderKey(instrument, activeAccount, activeStrategy, indicatorSeries) {
    const indicatorKey = indicatorSeries
        .map((series) => `${series.strategy_id || ''}:${series.account_id || ''}:${series.indicator_id || ''}`)
        .join('|');
    return `${instrument}::${activeAccount}::${activeStrategy || ''}::${indicatorKey}`;
}

function buildChartSeriesIdentityKey(indicatorSeries) {
    const indicatorKey = indicatorSeries
        .map((series) => `${series.strategy_id || ''}:${series.account_id || ''}:${series.indicator_id || ''}`)
        .join('|');
    return indicatorKey;
}

function buildChartMarkerKey(signals) {
    return JSON.stringify((signals || []).map((signal) => ({
        time: signal.time,
        price: signal.price,
        position: signal.position,
        color: signal.color,
        shape: signal.shape,
        text: signal.text
    })));
}

function clearTradeMarkerLayer() {
    if (chartRuntime.markerLayer) {
        chartRuntime.markerLayer.remove();
    }
    chartRuntime.markerLayer = null;
}

function ensureTradeMarkerLayer() {
    if (chartRuntime.markerLayer && chartRuntime.markerLayer.parentElement === tradingChart) {
        return chartRuntime.markerLayer;
    }

    clearTradeMarkerLayer();
    const layer = document.createElement('div');
    layer.className = 'trade-marker-layer';
    tradingChart.append(layer);
    chartRuntime.markerLayer = layer;
    return layer;
}

function renderPriceCoordinateTradeMarkers(instance, candleSeries, markerPayload) {
    if (!instance?.timeScale || typeof candleSeries?.priceToCoordinate !== 'function') {
        clearTradeMarkerLayer();
        return false;
    }

    const timeScale = instance.timeScale();
    if (typeof timeScale?.timeToCoordinate !== 'function') {
        clearTradeMarkerLayer();
        return false;
    }

    const layer = ensureTradeMarkerLayer();
    const fragment = document.createDocumentFragment();
    const width = Math.max(tradingChart.clientWidth, 0);
    const height = Math.max(tradingChart.clientHeight, 0);

    markerPayload.forEach((signal) => {
        const price = Number(signal.price);
        if (!Number.isFinite(price) || price <= 0) {
            return;
        }

        const x = timeScale.timeToCoordinate(signal.time);
        const y = candleSeries.priceToCoordinate(price);
        if (!Number.isFinite(x) || !Number.isFinite(y) || x < 0 || x > width || y < 0 || y > height) {
            return;
        }

        const isSell = signal.shape === 'arrowDown' || signal.position === 'aboveBar';
        const marker = document.createElement('div');
        marker.className = `trade-marker trade-marker--${isSell ? 'sell' : 'buy'}`;
        marker.style.left = `${x}px`;
        marker.style.top = `${y}px`;
        marker.title = signal.text || `${isSell ? 'Sell' : 'Buy'} @ ${formatDisplayNumber(price)}`;

        const label = document.createElement('span');
        label.className = 'trade-marker__label';
        label.textContent = signal.text || formatDisplayNumber(price);
        marker.append(label);
        fragment.append(marker);
    });

    layer.replaceChildren(fragment);
    return true;
}

function renderChartSignalMarkers(instance, candleSeries, markerPayload, createSeriesMarkers) {
    chartRuntime.currentMarkerPayload = Array.isArray(markerPayload)
        ? markerPayload.map((signal) => ({ ...signal }))
        : [];

    const nextMarkerKey = buildChartMarkerKey(chartRuntime.currentMarkerPayload);
    let overlayRendered = true;
    if (chartRuntime.lastOverlayMarkerKey !== nextMarkerKey || !chartRuntime.markerLayer) {
        overlayRendered = renderPriceCoordinateTradeMarkers(instance, candleSeries, chartRuntime.currentMarkerPayload);
        chartRuntime.lastOverlayMarkerKey = overlayRendered ? nextMarkerKey : '';
    }
    if (typeof createSeriesMarkers === 'function') {
        const nativeMarkers = overlayRendered
            ? []
            : chartRuntime.currentMarkerPayload.map((signal) => ({
                time: signal.time,
                position: signal.position,
                color: signal.color,
                shape: signal.shape,
                text: signal.text
            }));
        const nativeMarkerKey = overlayRendered ? '[]' : nextMarkerKey;
        if (chartRuntime.lastMarkerKey !== nativeMarkerKey) {
            createSeriesMarkers(candleSeries, nativeMarkers);
            chartRuntime.lastMarkerKey = nativeMarkerKey;
        }
    } else {
        chartRuntime.lastMarkerKey = nextMarkerKey;
    }
}

function resetChartMarkerRedrawSubscription() {
    if (!chartRuntime.instance || !chartRuntime.markerRedrawHandler) {
        chartRuntime.markerRedrawHandler = null;
        return;
    }

    const timeScale = chartRuntime.instance.timeScale?.();
    if (typeof timeScale?.unsubscribeVisibleTimeRangeChange === 'function') {
        timeScale.unsubscribeVisibleTimeRangeChange(chartRuntime.markerRedrawHandler);
    }
    chartRuntime.markerRedrawHandler = null;
}

function subscribeChartMarkerRedraw(instance, candleSeries) {
    if (chartRuntime.markerRedrawHandler || !instance?.timeScale || !candleSeries) {
        return;
    }

    const timeScale = instance.timeScale();
    if (typeof timeScale?.subscribeVisibleTimeRangeChange !== 'function') {
        return;
    }

    chartRuntime.markerRedrawHandler = () => {
        window.requestAnimationFrame(() => {
            if (chartRuntime.instance !== instance || chartRuntime.candleSeries !== candleSeries) {
                return;
            }
            renderPriceCoordinateTradeMarkers(instance, candleSeries, chartRuntime.currentMarkerPayload);
        });
    };
    timeScale.subscribeVisibleTimeRangeChange(chartRuntime.markerRedrawHandler);
}

function chartBarsEqual(left, right) {
    return left?.time === right?.time
        && left?.open === right?.open
        && left?.high === right?.high
        && left?.low === right?.low
        && left?.close === right?.close;
}

function chartIndicatorPointsEqual(left, right) {
    return left?.time === right?.time
        && left?.value === right?.value;
}

function findIncrementalTailStart(previousPoints, nextPoints, pointsEqual) {
    if (!Array.isArray(previousPoints) || previousPoints.length === 0) {
        return 0;
    }
    if (!Array.isArray(nextPoints) || nextPoints.length === 0 || nextPoints.length < previousPoints.length) {
        return null;
    }

    const stablePrefixLength = Math.max(0, Math.min(previousPoints.length, nextPoints.length) - 1);
    for (let index = 0; index < stablePrefixLength; index += 1) {
        if (!pointsEqual(previousPoints[index], nextPoints[index])) {
            return null;
        }
    }

    let tailStart = stablePrefixLength;
    while (tailStart < previousPoints.length
        && tailStart < nextPoints.length
        && pointsEqual(previousPoints[tailStart], nextPoints[tailStart])) {
        tailStart += 1;
    }

    return tailStart;
}

function cloneChartBars(bars) {
    return Array.isArray(bars) ? bars.map((bar) => ({ ...bar })) : [];
}

function cloneChartIndicatorPoints(points) {
    return Array.isArray(points) ? points.map((point) => ({ ...point })) : [];
}

function syncChartSeriesData(seriesHandle, nextPoints, previousPoints, pointsEqual, clonePoints) {
    if (!seriesHandle) {
        return clonePoints(nextPoints);
    }

    if (!Array.isArray(nextPoints) || nextPoints.length === 0) {
        if (Array.isArray(previousPoints) && previousPoints.length > 0) {
            seriesHandle.setData([]);
        }
        return [];
    }

    const tailStart = findIncrementalTailStart(previousPoints, nextPoints, pointsEqual);
    if (tailStart === null) {
        seriesHandle.setData(nextPoints);
        return clonePoints(nextPoints);
    }

    if (tailStart === 0) {
        seriesHandle.setData(nextPoints);
        return clonePoints(nextPoints);
    }

    for (let index = tailStart; index < nextPoints.length; index += 1) {
        seriesHandle.update(nextPoints[index]);
    }

    return clonePoints(nextPoints);
}

function normalizeLiveInventoryAdjustment(adjustment, index = 0) {
    return {
        id: String(adjustment?.id ?? `adjustment_${index + 1}`).trim(),
        enabled: adjustment?.enabled !== false && String(adjustment?.enabled ?? 'true').trim().toLowerCase() !== 'false',
        account_id: String(adjustment?.account_id ?? '').trim(),
        strategy_id: String(adjustment?.strategy_id ?? 'external_manual').trim(),
        instrument: String(adjustment?.instrument ?? '').trim(),
        exchange: String(adjustment?.exchange ?? '').trim(),
        operator_id: String(adjustment?.operator_id ?? '').trim(),
        reason_code: String(adjustment?.reason_code ?? '').trim(),
        reason_text: String(adjustment?.reason_text ?? '').trim(),
        long_today_delta: String(adjustment?.long_today_delta ?? '0').trim() || '0',
        long_today_average_price: String(adjustment?.long_today_average_price ?? '').trim(),
        long_yesterday_delta: String(adjustment?.long_yesterday_delta ?? '0').trim() || '0',
        long_yesterday_average_price: String(adjustment?.long_yesterday_average_price ?? '').trim(),
        short_today_delta: String(adjustment?.short_today_delta ?? '0').trim() || '0',
        short_today_average_price: String(adjustment?.short_today_average_price ?? '').trim(),
        short_yesterday_delta: String(adjustment?.short_yesterday_delta ?? '0').trim() || '0',
        short_yesterday_average_price: String(adjustment?.short_yesterday_average_price ?? '').trim(),
        applied: adjustment?.applied === true || String(adjustment?.applied ?? 'false').trim().toLowerCase() === 'true',
        applied_at: String(adjustment?.applied_at ?? '').trim()
    };
}

function normalizeLiveInventoryReconciliation(reconciliation) {
    return {
        account_id: String(reconciliation?.account_id ?? '').trim(),
        aggregate_match: reconciliation?.aggregate_match === true || String(reconciliation?.aggregate_match ?? 'false').trim().toLowerCase() === 'true',
        applied_adjustment_count: String(reconciliation?.applied_adjustment_count ?? '0').trim() || '0',
        broker_snapshot_timestamp: String(reconciliation?.broker_snapshot_timestamp ?? '').trim(),
        mismatch_summary: String(reconciliation?.mismatch_summary ?? '').trim(),
        manual_adjustments_path: String(reconciliation?.manual_adjustments_path ?? '').trim()
    };
}

function normalizeLiveInventoryPersistedPosition(position) {
    return {
        store_path: String(position?.store_path ?? '').trim(),
        store_namespace: String(position?.store_namespace ?? '').trim(),
        account_id: String(position?.account_id ?? '').trim(),
        strategy_id: String(position?.strategy_id ?? '').trim(),
        instrument: String(position?.instrument ?? '').trim(),
        long_today_quantity: String(position?.long_today_quantity ?? '0').trim() || '0',
        long_yesterday_quantity: String(position?.long_yesterday_quantity ?? '0').trim() || '0',
        long_quantity: String(position?.long_quantity ?? '0').trim() || '0',
        long_average_price: String(position?.long_average_price ?? '').trim(),
        short_today_quantity: String(position?.short_today_quantity ?? '0').trim() || '0',
        short_yesterday_quantity: String(position?.short_yesterday_quantity ?? '0').trim() || '0',
        short_quantity: String(position?.short_quantity ?? '0').trim() || '0',
        short_average_price: String(position?.short_average_price ?? '').trim(),
        net: String(position?.net ?? '0').trim() || '0'
    };
}

function normalizeLiveInventoryPayload(payload) {
    return {
        config_path: String(payload?.config_path ?? '').trim(),
        adjustments_path: String(payload?.adjustments_path ?? '').trim(),
        store_path: String(payload?.store_path ?? '').trim(),
        adjustments_exists: payload?.adjustments_exists === true || String(payload?.adjustments_exists ?? 'false').trim().toLowerCase() === 'true',
        store_exists: payload?.store_exists === true || String(payload?.store_exists ?? 'false').trim().toLowerCase() === 'true',
        store_updated_at: String(payload?.store_updated_at ?? '').trim(),
        adjustments: Array.isArray(payload?.adjustments)
            ? payload.adjustments.map((adjustment, index) => normalizeLiveInventoryAdjustment(adjustment, index))
            : [],
        persisted_positions: Array.isArray(payload?.persisted_positions)
            ? payload.persisted_positions.map((position) => normalizeLiveInventoryPersistedPosition(position))
            : [],
        reconciliations: Array.isArray(payload?.reconciliations)
            ? payload.reconciliations.map((reconciliation) => normalizeLiveInventoryReconciliation(reconciliation))
            : [],
        warnings: Array.isArray(payload?.warnings) ? payload.warnings : []
    };
}

function normalizeRecorderPayload(payload) {
    const mdFront = String(payload?.md_front ?? payload?.front ?? '').trim();
    return {
        config_path: String(payload?.config_path ?? 'configs/ctp_md_recorder.ini').trim(),
        launch_script: String(payload?.launch_script ?? 'scripts/start_recorder_release.ps1').trim(),
        exists: payload?.exists === true || String(payload?.exists ?? 'false').trim().toLowerCase() === 'true',
        account_section: String(payload?.account_section ?? 'account.recorder').trim(),
        account_id: String(payload?.account_id ?? 'recorder').trim() || 'recorder',
        output_dir: String(payload?.output_dir ?? '../runtime/ctp_md_recorder/agtick').trim() || '../runtime/ctp_md_recorder/agtick',
        instruments: String(payload?.instruments ?? 'ag2606').trim() || 'ag2606',
        flush_interval_ms: String(payload?.flush_interval_ms ?? '1000').trim() || '1000',
        status_interval_ms: String(payload?.status_interval_ms ?? '30000').trim() || '30000',
        idle_sleep_ms: String(payload?.idle_sleep_ms ?? '250').trim() || '250',
        connect_timeout_ms: String(payload?.connect_timeout_ms ?? '15000').trim() || '15000',
        deduplicate_exact_ticks: String(payload?.deduplicate_exact_ticks ?? 'true').trim() || 'true',
        auto_restart_enabled: String(payload?.auto_restart_enabled ?? 'true').trim() || 'true',
        front: mdFront,
        md_front: mdFront,
        broker_id: String(payload?.broker_id ?? '').trim(),
        user_id: String(payload?.user_id ?? '').trim(),
        password: String(payload?.password ?? '').trim(),
        product_info: String(payload?.product_info ?? 'iTrader').trim() || 'iTrader',
        flow_dir: String(payload?.flow_dir ?? '').trim(),
        production_mode: String(payload?.production_mode ?? 'true').trim() || 'true',
        reconnect_enabled: String(payload?.reconnect_enabled ?? 'true').trim() || 'true',
        reconnect_retry_interval_ms: String(payload?.reconnect_retry_interval_ms ?? '3000').trim() || '3000',
        reconnect_max_attempts: String(payload?.reconnect_max_attempts ?? '0').trim() || '0'
    };
}

function getRecorderState() {
    if (!state.recorder || typeof state.recorder !== 'object') {
        state.recorder = normalizeRecorderPayload({});
    }

    return state.recorder;
}

function normalizeRecorderRuntimePayload(payload) {
    const normalizedStatus = ['running', 'stopped', 'failed'].includes(String(payload?.status ?? '').trim().toLowerCase())
        ? String(payload.status).trim().toLowerCase()
        : 'stopped';
    const processId = Number(payload?.process_id ?? 0);
    const exitCode = Number(payload?.exit_code ?? 0);
    const startedAtMs = Number(payload?.started_at_ms ?? 0);
    const finishedAtMs = Number(payload?.finished_at_ms ?? 0);
    const autoRestartCount = Number(payload?.auto_restart_count ?? 0);
    const lastAutoRestartAtMs = Number(payload?.last_auto_restart_at_ms ?? 0);
    const managedByRaw = String(payload?.managed_by ?? '').trim().toLowerCase();
    const managedBy = ['dashboard', 'scheduled_task', 'external'].includes(managedByRaw)
        ? managedByRaw
        : '';

    return {
        status: normalizedStatus,
        running: payload?.running === true || normalizedStatus === 'running',
        stop_requested: payload?.stop_requested === true || String(payload?.stop_requested ?? '').trim().toLowerCase() === 'true',
        process_id: Number.isFinite(processId) ? processId : 0,
        exit_code: Number.isFinite(exitCode) ? exitCode : 0,
        auto_restart_enabled: payload?.auto_restart_enabled === true || String(payload?.auto_restart_enabled ?? '').trim().toLowerCase() === 'true',
        auto_restart_count: Number.isFinite(autoRestartCount) ? autoRestartCount : 0,
        last_auto_restart_at_ms: Number.isFinite(lastAutoRestartAtMs) ? lastAutoRestartAtMs : 0,
        managed_by: managedBy,
        controller_name: String(payload?.controller_name ?? '').trim(),
        executable_path: String(payload?.executable_path ?? '').trim(),
        config_path: String(payload?.config_path ?? '').trim(),
        started_at_ms: Number.isFinite(startedAtMs) ? startedAtMs : 0,
        finished_at_ms: Number.isFinite(finishedAtMs) ? finishedAtMs : 0,
        message: String(payload?.message ?? '').trim()
    };
}

function formatRecorderManager(recorderRuntime) {
    const managedBy = String(recorderRuntime?.managed_by ?? '').trim().toLowerCase();
    const controllerName = String(recorderRuntime?.controller_name ?? '').trim();

    if (managedBy === 'scheduled_task') {
        return controllerName ? `Scheduled task: ${controllerName}` : 'Scheduled task';
    }
    if (managedBy === 'external') {
        return 'External process';
    }
    if (managedBy === 'dashboard') {
        return 'Dashboard';
    }
    return 'Unknown';
}

function getRecorderRuntimeState() {
    if (!state.recorder_runtime || typeof state.recorder_runtime !== 'object') {
        state.recorder_runtime = normalizeRecorderRuntimePayload({});
    }

    return state.recorder_runtime;
}

function isLiveDryRunEnabled() {
    return String(state.live?.dry_run ?? 'false').trim().toLowerCase() === 'true';
}

function setLiveDryRunEnabled(enabled) {
    state.live.dry_run = enabled ? 'true' : 'false';
}

function applyRecorderRuntimePayload(payload) {
    state.recorder_runtime = normalizeRecorderRuntimePayload(payload);
    return state.recorder_runtime;
}

function getLiveInventoryState() {
    if (!state.live_inventory || typeof state.live_inventory !== 'object') {
        state.live_inventory = normalizeLiveInventoryPayload({});
    }

    return state.live_inventory;
}

function nextLiveInventoryAdjustmentId(accountId) {
    const inventory = getLiveInventoryState();
    const baseId = sanitizeStrategyId(`${accountId || 'account'}_manual_adjustment`);
    let candidate = baseId;
    let suffix = 2;

    while (inventory.adjustments.some((adjustment) => adjustment.id === candidate)) {
        candidate = `${baseId}_${suffix}`;
        suffix += 1;
    }

    return candidate;
}

function createBlankLiveInventoryAdjustment(accountId) {
    return normalizeLiveInventoryAdjustment({
        id: nextLiveInventoryAdjustmentId(accountId),
        enabled: true,
        account_id: accountId,
        strategy_id: 'external_manual',
        instrument: '',
        exchange: '',
        operator_id: 'dashboard',
        reason_code: 'manual_adjustment',
        reason_text: '',
        long_today_delta: '0',
        long_today_average_price: '',
        long_yesterday_delta: '0',
        long_yesterday_average_price: '',
        short_today_delta: '0',
        short_today_average_price: '',
        short_yesterday_delta: '0',
        short_yesterday_average_price: '',
        applied: false,
        applied_at: ''
    });
}

function cloneLiveInventoryAdjustment(adjustment) {
    return normalizeLiveInventoryAdjustment({
        ...adjustment,
        id: nextLiveInventoryAdjustmentId(adjustment.account_id),
        applied: false,
        applied_at: ''
    });
}

function getAccountLiveInventoryAdjustments(accountId) {
    return getLiveInventoryState().adjustments.filter((adjustment) => adjustment.account_id === accountId);
}

function getAccountPersistedInventoryPositions(accountId) {
    return getLiveInventoryState().persisted_positions.filter((position) => position.account_id === accountId);
}

function summarizePersistedInventoryPositions(positions) {
    const longQuantity = positions.reduce((total, position) => total + Number(position.long_quantity ?? 0), 0);
    const shortQuantity = positions.reduce((total, position) => total + Number(position.short_quantity ?? 0), 0);
    const netQuantity = positions.reduce((total, position) => total + Number(position.net ?? 0), 0);
    return {
        longQuantity,
        shortQuantity,
        netQuantity,
        nonFlatCount: positions.filter((position) => Number(position.long_quantity ?? 0) !== 0 || Number(position.short_quantity ?? 0) !== 0).length
    };
}

function integerText(value) {
    const raw = String(value ?? '0').trim();
    return raw || '0';
}

function recomputePersistedInventoryPosition(position) {
    const longToday = Number(integerText(position.long_today_quantity));
    const longYesterday = Number(integerText(position.long_yesterday_quantity));
    const shortToday = Number(integerText(position.short_today_quantity));
    const shortYesterday = Number(integerText(position.short_yesterday_quantity));
    const longQuantity = (Number.isFinite(longToday) ? longToday : 0) + (Number.isFinite(longYesterday) ? longYesterday : 0);
    const shortQuantity = (Number.isFinite(shortToday) ? shortToday : 0) + (Number.isFinite(shortYesterday) ? shortYesterday : 0);
    position.long_quantity = String(longQuantity);
    position.short_quantity = String(shortQuantity);
    position.net = String(longQuantity - shortQuantity);
    return position;
}

function findPersistedInventoryPositionIndex(accountId, strategyId, instrument) {
    const normalizedAccount = String(accountId ?? '').trim();
    const normalizedStrategy = String(strategyId ?? '').trim();
    const normalizedInstrument = String(instrument ?? '').trim().toLowerCase();
    return getLiveInventoryState().persisted_positions.findIndex((position) =>
        String(position.account_id ?? '').trim() === normalizedAccount
        && String(position.strategy_id ?? '').trim() === normalizedStrategy
        && String(position.instrument ?? '').trim().toLowerCase() === normalizedInstrument);
}

function findPersistedInventoryPositionIndexInList(positions, accountId, strategyId, instrument) {
    const normalizedAccount = String(accountId ?? '').trim();
    const normalizedStrategy = String(strategyId ?? '').trim();
    const normalizedInstrument = String(instrument ?? '').trim().toLowerCase();
    return positions.findIndex((position) =>
        String(position.account_id ?? '').trim() === normalizedAccount
        && String(position.strategy_id ?? '').trim() === normalizedStrategy
        && String(position.instrument ?? '').trim().toLowerCase() === normalizedInstrument);
}

function parseInventoryStorePath(storePath) {
    const normalized = String(storePath ?? '').trim().replaceAll('\\', '/');
    const match = normalized.match(/^(.*\/runtime)\/([^/]+)\/strategy_inventory_store\.ini$/i);
    return match
        ? { runtimeRoot: match[1], namespace: match[2], normalized }
        : null;
}

function inferPersistedInventoryStorePath(position) {
    const explicit = String(position?.store_path ?? '').trim();
    if (explicit) {
        return explicit;
    }

    const inventory = getLiveInventoryState();
    const currentStorePath = String(inventory.store_path ?? '').trim();
    const parsed = parseInventoryStorePath(currentStorePath);
    const strategyId = String(position?.strategy_id ?? '').trim();
    if (parsed && strategyId) {
        if (parsed.namespace.includes('_dashboard_')) {
            return currentStorePath;
        }
        return `${parsed.runtimeRoot}/${parsed.namespace}_dashboard_${strategyId}/strategy_inventory_store.ini`;
    }

    return currentStorePath;
}

function inferPersistedInventoryStoreNamespace(storePath) {
    return parseInventoryStorePath(storePath)?.namespace ?? '';
}

function createPersistedInventoryPositionFromRuntimePosition(position, accountId) {
    const storePath = inferPersistedInventoryStorePath(position);
    const persisted = normalizeLiveInventoryPersistedPosition({
        store_path: storePath,
        store_namespace: inferPersistedInventoryStoreNamespace(storePath),
        account_id: position?.account_id || accountId,
        strategy_id: position?.strategy_id || '',
        instrument: position?.instrument || '',
        long_today_quantity: position?.long_today_quantity ?? '0',
        long_yesterday_quantity: position?.long_yesterday_quantity ?? '0',
        long_average_price: position?.long_average_price ?? '0',
        short_today_quantity: position?.short_today_quantity ?? '0',
        short_yesterday_quantity: position?.short_yesterday_quantity ?? '0',
        short_average_price: position?.short_average_price ?? '0'
    });
    return recomputePersistedInventoryPosition(persisted);
}

function applyPersistedInventoryPositionFieldEdit(position, fieldKey, value) {
    if (fieldKey === 'long_quantity') {
        position.long_today_quantity = integerText(value);
        position.long_yesterday_quantity = '0';
    } else if (fieldKey === 'short_quantity') {
        position.short_today_quantity = integerText(value);
        position.short_yesterday_quantity = '0';
    } else {
        position[fieldKey] = value;
    }
    return recomputePersistedInventoryPosition(position);
}

function updatePersistedInventoryPositionField(globalIndex, fieldKey, value) {
    const inventory = getLiveInventoryState();
    const positions = cloneLiveInventoryPersistedPositions(inventory.persisted_positions);
    if (globalIndex < 0 || globalIndex >= positions.length) {
        return;
    }

    applyPersistedInventoryPositionFieldEdit(positions[globalIndex], fieldKey, value);
    setLocalPersistedInventoryDraftPositions(positions);
}

function updateOrCreatePersistedInventoryPositionField(sourcePosition, accountId, fieldKey, value) {
    const inventory = getLiveInventoryState();
    const positions = cloneLiveInventoryPersistedPositions(inventory.persisted_positions);
    let index = findPersistedInventoryPositionIndexInList(
        positions,
        sourcePosition?.account_id || accountId,
        sourcePosition?.strategy_id,
        sourcePosition?.instrument
    );

    if (index < 0) {
        positions.push(createPersistedInventoryPositionFromRuntimePosition(sourcePosition, accountId));
        index = positions.length - 1;
    }

    applyPersistedInventoryPositionFieldEdit(positions[index], fieldKey, value);
    setLocalPersistedInventoryDraftPositions(positions);
}

function getLiveInventoryReconciliation(accountId) {
    return getLiveInventoryState().reconciliations.find((reconciliation) => reconciliation.account_id === accountId) || null;
}

function normalizeLiveRuntimeStrategyIds(values) {
    if (!Array.isArray(values)) {
        return [];
    }

    return uniqueValues(values
        .map((value) => String(value ?? '').trim())
        .filter(Boolean))
        .sort((left, right) => left.localeCompare(right));
}

function normalizeLiveRuntimeInstancePayload(payload, requestedConfigPath = '') {
    const normalizedStatus = ['running', 'stopped', 'failed'].includes(String(payload?.status ?? '').trim().toLowerCase())
        ? String(payload.status).trim().toLowerCase()
        : 'stopped';
    const activeConfigPath = String(payload?.active_config_path ?? payload?.config_path ?? '').trim();
    const requestedPath = String(payload?.requested_config_path ?? requestedConfigPath ?? '').trim();
    const explicitMatch = payload?.config_matches_request;
    const derivedMatch = !activeConfigPath || !requestedPath
        || normalizePathForComparison(activeConfigPath).toLowerCase() === normalizePathForComparison(requestedPath).toLowerCase();
    const processId = Number(payload?.process_id ?? 0);
    const exitCode = Number(payload?.exit_code ?? 0);
    const startedAtMs = Number(payload?.started_at_ms ?? 0);
    const finishedAtMs = Number(payload?.finished_at_ms ?? 0);

    return {
        status: normalizedStatus,
        running: payload?.running === true || normalizedStatus === 'running',
        stop_requested: payload?.stop_requested === true || String(payload?.stop_requested ?? '').trim().toLowerCase() === 'true',
        process_id: Number.isFinite(processId) ? processId : 0,
        exit_code: Number.isFinite(exitCode) ? exitCode : 0,
        executable_path: String(payload?.executable_path ?? '').trim(),
        active_config_path: activeConfigPath,
        requested_config_path: requestedPath,
        strategy_ids: normalizeLiveRuntimeStrategyIds(payload?.strategy_ids),
        config_matches_request: explicitMatch === false ? false : (explicitMatch === true ? true : derivedMatch),
        started_at_ms: Number.isFinite(startedAtMs) ? startedAtMs : 0,
        finished_at_ms: Number.isFinite(finishedAtMs) ? finishedAtMs : 0,
        message: String(payload?.message ?? '').trim()
    };
}

function normalizeLiveRuntimePayload(payload, requestedConfigPath = '') {
    const normalized = normalizeLiveRuntimeInstancePayload(payload, requestedConfigPath);
    normalized.runtime_count = Number.isFinite(Number(payload?.runtime_count)) ? Number(payload.runtime_count) : 0;
    normalized.running_count = Number.isFinite(Number(payload?.running_count)) ? Number(payload.running_count) : (normalized.running ? 1 : 0);
    normalized.instances = Array.isArray(payload?.instances)
        ? payload.instances.map((instance) => normalizeLiveRuntimeInstancePayload(instance, requestedConfigPath))
        : [];
    return normalized;
}

function getLiveRuntimeInstances(liveRuntime = getLiveRuntimeState()) {
    return Array.isArray(liveRuntime?.instances) && liveRuntime.instances.length > 0
        ? liveRuntime.instances
        : (liveRuntime ? [liveRuntime] : []);
}

function liveRuntimeInstanceTargetsStrategy(liveRuntime, strategy, accountId) {
    if (!liveRuntime?.config_matches_request || !strategy?.accounts?.includes(accountId)) {
        return false;
    }

    const strategyIds = Array.isArray(liveRuntime.strategy_ids) ? liveRuntime.strategy_ids : [];
    return strategyIds.length === 0 || strategyIds.includes(strategy.id);
}

function liveRuntimeTargetsStrategy(liveRuntime, strategy, accountId) {
    return getLiveRuntimeInstances(liveRuntime).some((instance) => liveRuntimeInstanceTargetsStrategy(instance, strategy, accountId));
}

function findLiveRuntimeInstanceForStrategy(strategy, accountId, { statuses = [] } = {}) {
    const statusSet = new Set(statuses.map((status) => String(status).trim().toLowerCase()).filter(Boolean));
    return getLiveRuntimeInstances().find((instance) => {
        if (statusSet.size > 0 && !statusSet.has(instance.status)) {
            return false;
        }
        return liveRuntimeInstanceTargetsStrategy(instance, strategy, accountId);
    }) || null;
}

function getLiveRuntimeState() {
    if (!state.live_runtime || typeof state.live_runtime !== 'object') {
        state.live_runtime = normalizeLiveRuntimePayload({});
    }

    return state.live_runtime;
}

function applyLiveRuntimePayload(payload) {
    state.live_runtime = normalizeLiveRuntimePayload(payload, runtime.sourceConfig);
    return state.live_runtime;
}

function clearLiveRuntimePollTimer() {
    if (runtime.liveRuntimePollTimer !== null) {
        window.clearTimeout(runtime.liveRuntimePollTimer);
        runtime.liveRuntimePollTimer = null;
    }
}

function clearRecorderRuntimePollTimer() {
    if (runtime.recorderRuntimePollTimer !== null) {
        window.clearTimeout(runtime.recorderRuntimePollTimer);
        runtime.recorderRuntimePollTimer = null;
    }
}

function clearLiveStateReconnectTimer() {
    if (runtime.liveStateReconnectTimer !== null) {
        window.clearTimeout(runtime.liveStateReconnectTimer);
        runtime.liveStateReconnectTimer = null;
    }
}

function scheduleLiveStateReconnect() {
    if (state.mode !== 'live' || !runtime.apiConnected || runtime.liveStateEventSource || runtime.liveStateReconnectTimer !== null) {
        return;
    }

    const attempt = runtime.liveStateReconnectAttempt;
    const delayMs = Math.min(10000, 1000 * Math.max(1, 2 ** attempt));
    runtime.liveStateReconnectTimer = window.setTimeout(() => {
        runtime.liveStateReconnectTimer = null;
        openLiveStateStream();
    }, delayMs);
    runtime.liveStateReconnectAttempt = Math.min(attempt + 1, 5);
}

function closeLiveStateStream() {
    clearLiveStateReconnectTimer();
    if (runtime.liveStateEventSource) {
        runtime.liveStateEventSource.close();
        runtime.liveStateEventSource = null;
    }
    runtime.liveStateStreamConnected = false;
    runtime.liveStateReconnectAttempt = 0;
}

function openLiveStateStream() {
    if (state.mode !== 'live' || !runtime.apiConnected) {
        closeLiveStateStream();
        return;
    }

    if (runtime.liveStateEventSource) {
        return;
    }

    clearLiveStateReconnectTimer();

    const query = buildModeQuery('live');
    const streamUrl = `${API_BASE}/api/state/stream?${query.toString()}`;
    const eventSource = new EventSource(streamUrl);
    runtime.liveStateEventSource = eventSource;
    runtime.liveStateStreamConnected = false;

    eventSource.onopen = () => {
        runtime.liveStateStreamConnected = true;
        runtime.liveStateReconnectAttempt = 0;
    };

    eventSource.addEventListener('state', (event) => {
        try {
            const payload = JSON.parse(event.data);
            applyState(normalizeIncomingState(payload, 'live'));
            runtime.apiConnected = payload.api?.connected ?? true;
            runtime.sourceConfig = payload.api?.source_config ?? runtime.sourceConfig;
            runtime.liveStateStreamConnected = true;
            runtime.liveStateLastStateAtMs = Date.now();
            render();
        } catch (error) {
            runtime.liveStateStreamConnected = false;
        }
    });

    eventSource.onerror = () => {
        runtime.liveStateStreamConnected = false;
        if (runtime.liveStateEventSource === eventSource) {
            eventSource.close();
            runtime.liveStateEventSource = null;
        }
        scheduleLiveStateReconnect();
    };
}

function syncStrategyStatusesFromLiveRuntime(liveRuntime) {
    state.strategies.forEach((strategy) => {
        if (strategy.accounts.length === 0) {
            return;
        }

        const runtimeRunningForStrategy = strategy.accounts.some((accountId) => (
            getLiveRuntimeInstances(liveRuntime)
                .some((instance) => instance.status === 'running' && liveRuntimeInstanceTargetsStrategy(instance, strategy, accountId))
        ));

        if (runtimeRunningForStrategy) {
            strategy.__runtimeStatus = 'running';
            strategy.__runtimeError = '';
            return;
        }

        const runtimeFailedForStrategy = strategy.accounts.some((accountId) => (
            getLiveRuntimeInstances(liveRuntime)
                .some((instance) => instance.status === 'failed' && liveRuntimeInstanceTargetsStrategy(instance, strategy, accountId))
        ));

        if (runtimeFailedForStrategy) {
            strategy.__runtimeStatus = 'failed';
            strategy.__runtimeError = liveRuntime.message || 'Live runtime failed.';
            return;
        }

        strategy.__runtimeStatus = 'stopped';
        strategy.__runtimeError = '';
    });
}

function scheduleLiveRuntimePoll(delayMs = 2000) {
    clearLiveRuntimePollTimer();
    if (state.mode !== 'live' || !runtime.apiConnected) {
        return;
    }

    runtime.liveRuntimePollTimer = window.setTimeout(() => {
        pollLiveRuntimeStatus({ silent: true }).catch(() => {
            clearLiveRuntimePollTimer();
        });
    }, delayMs);
}

function scheduleRecorderRuntimePoll(delayMs = 2500) {
    clearRecorderRuntimePollTimer();
    if (state.mode !== 'live' || !runtime.apiConnected) {
        return;
    }

    runtime.recorderRuntimePollTimer = window.setTimeout(() => {
        pollRecorderRuntimeStatus({ silent: true }).catch(() => {
            clearRecorderRuntimePollTimer();
        });
    }, delayMs);
}

async function pollLiveRuntimeStatus({ silent = false } = {}) {
    if (state.mode !== 'live' || !runtime.apiConnected) {
        clearLiveRuntimePollTimer();
        return;
    }

    if (runtime.pendingLiveControlAction) {
        scheduleLiveRuntimePoll(1200);
        return;
    }

    try {
        const response = await fetch(`${API_BASE}/api/live-run?${buildModeQuery('live').toString()}`);
        if (!response.ok) {
            const detail = await response.text();
            throw new Error(detail || `HTTP ${response.status}`);
        }

        const payload = await response.json();
        const previous = { ...getLiveRuntimeState() };
        const liveRuntime = applyLiveRuntimePayload(payload);
        const strategyScopeChanged = JSON.stringify(previous.strategy_ids ?? []) !== JSON.stringify(liveRuntime.strategy_ids ?? []);
        const instancesChanged = JSON.stringify(previous.instances ?? []) !== JSON.stringify(liveRuntime.instances ?? []);
        const statusChanged = previous.status !== liveRuntime.status
            || previous.stop_requested !== liveRuntime.stop_requested
            || previous.process_id !== liveRuntime.process_id
            || previous.active_config_path !== liveRuntime.active_config_path
            || previous.config_matches_request !== liveRuntime.config_matches_request
            || strategyScopeChanged
            || instancesChanged;

        if (statusChanged) {
            syncStrategyStatusesFromLiveRuntime(liveRuntime);
            if (payload.message) {
                runtime.lastMessage = payload.message;
            }
        }

        let snapshotChanged = false;
        const liveStateSilentForMs = Date.now() - Number(runtime.liveStateLastStateAtMs || 0);
        const shouldFallbackToSnapshot = isLiveRuntimeActiveForCurrentConfig()
            && !runtime.liveStateStreamConnected
            && liveStateSilentForMs > 8000;

        if (shouldFallbackToSnapshot) {
            try {
                const modePayload = await fetchModeStatePayload('live');
                applyState(normalizeIncomingState(modePayload, 'live'));
                runtime.apiConnected = modePayload.api?.connected ?? true;
                runtime.sourceConfig = modePayload.api?.source_config ?? runtime.sourceConfig;
                snapshotChanged = true;
            } catch (error) {
                if (!silent) {
                    const detail = error instanceof Error ? error.message : String(error);
                    runtime.lastMessage = `Live snapshot refresh failed: ${detail}.`;
                }
            }
        }

        if (!silent || statusChanged || snapshotChanged) {
            render();
        }
    } catch (error) {
        if (!silent) {
            const detail = error instanceof Error ? error.message : String(error);
            runtime.lastMessage = `Unable to refresh the live runtime status: ${detail}.`;
            render();
        }
    } finally {
        scheduleLiveRuntimePoll(isLiveRuntimeActiveForCurrentConfig() ? 1500 : 3000);
    }
}

async function pollRecorderRuntimeStatus({ silent = false } = {}) {
    if (state.mode !== 'live' || !runtime.apiConnected) {
        clearRecorderRuntimePollTimer();
        return;
    }

    if (runtime.pendingRecorderControlAction) {
        scheduleRecorderRuntimePoll(1200);
        return;
    }

    try {
        const response = await fetch(`${API_BASE}/api/recorder-run`);
        if (!response.ok) {
            const detail = await response.text();
            throw new Error(detail || `HTTP ${response.status}`);
        }

        const payload = await response.json();
        const previous = { ...getRecorderRuntimeState() };
        const recorderRuntime = applyRecorderRuntimePayload(payload);
        const statusChanged = previous.status !== recorderRuntime.status
            || previous.stop_requested !== recorderRuntime.stop_requested
            || previous.process_id !== recorderRuntime.process_id;

        if (statusChanged && payload.message) {
            runtime.lastMessage = payload.message;
        }

        if (!silent || statusChanged) {
            render();
        }
    } catch (error) {
        if (!silent) {
            const detail = error instanceof Error ? error.message : String(error);
            runtime.lastMessage = `Unable to refresh the recorder status: ${detail}.`;
            render();
        }
    } finally {
        scheduleRecorderRuntimePoll(getRecorderRuntimeState().running ? 1500 : 3000);
    }
}

function isLiveRuntimeActiveForCurrentConfig() {
    const liveRuntime = getLiveRuntimeState();
    return state.mode === 'live'
        && getLiveRuntimeInstances(liveRuntime).some((instance) => instance.status === 'running' && instance.config_matches_request);
}

function isOtherLiveRuntimeActive() {
    const liveRuntime = getLiveRuntimeState();
    return state.mode === 'live'
        && getLiveRuntimeInstances(liveRuntime).some((instance) => instance.status === 'running' && !instance.config_matches_request);
}

function getEffectiveStrategyRuntimeStatus(strategy, accountId, { isPendingBacktestRun = false } = {}) {
    if (isPendingBacktestRun) {
        return 'running';
    }

    if (state.mode !== 'live') {
        return strategy.__runtimeStatus;
    }

    if (findLiveRuntimeInstanceForStrategy(strategy, accountId, { statuses: ['running'] })) {
        return 'running';
    }

    if (findLiveRuntimeInstanceForStrategy(strategy, accountId, { statuses: ['failed'] })) {
        return 'failed';
    }

    return strategy.__runtimeStatus === 'failed' ? 'failed' : 'stopped';
}

function normalizeIncomingState(payload, mode) {
    const fallback = structuredClone(sampleStates[mode]);
    const requestedConfigPath = String(payload?.api?.source_config ?? runtime.sourceConfig ?? '').trim();
    const normalized = {
        ...fallback,
        ...payload,
        accounts: Array.isArray(payload.accounts) && payload.accounts.length > 0 ? payload.accounts : fallback.accounts,
        strategies: Array.isArray(payload.strategies) ? payload.strategies : fallback.strategies,
        backtest: { ...fallback.backtest, ...(payload.backtest ?? {}) },
        live: { ...fallback.live, ...(payload.live ?? {}) },
        recorder: normalizeRecorderPayload(payload.recorder ?? fallback.recorder ?? {}),
        recorder_runtime: normalizeRecorderRuntimePayload(payload.recorder_runtime ?? fallback.recorder_runtime ?? {}),
        live_inventory: normalizeLiveInventoryPayload(payload.live_inventory ?? fallback.live_inventory ?? {}),
        live_runtime: normalizeLiveRuntimePayload(payload.live_runtime ?? fallback.live_runtime ?? {}, requestedConfigPath),
        equity: Array.isArray(payload.equity) && payload.equity.length > 1 ? payload.equity : fallback.equity,
        activity: Array.isArray(payload.activity) && payload.activity.length > 0 ? payload.activity : fallback.activity
    };

    normalized.chart = normalizeChartPayload(payload.chart, normalized);

    normalized.strategies.forEach((strategy) => {
        ensureStrategyUiState(strategy);
        normalizeStrategyAccounts(strategy);
        normalizeStrategyRuntimeDetails(strategy);
    });
    return normalized;
}

async function hydrateMode(mode, { preferSample = false } = {}) {
    applyModeButtons(mode);
    clearLiveRuntimePollTimer();
    clearRecorderRuntimePollTimer();
    clearLocalStrategiesDraft();
    clearLocalRecorderDraft();

    if (preferSample) {
        applyState(structuredClone(sampleStates[mode]));
        runtime.apiConnected = false;
        runtime.sourceConfig = 'sample-state';
        runtime.lastMessage = 'Reloaded built-in sample data.';
        resetStrategyFileCatalog();
        runtime.strategyFileCatalogStatus = 'error';
        runtime.strategyFileCatalogError = 'Local API is offline.';
        render();
        return;
    }

    try {
        const payload = await fetchModeStatePayload(mode);
        applyState(normalizeIncomingState(payload, mode));
        runtime.apiConnected = payload.api?.connected ?? true;
        runtime.sourceConfig = payload.api?.source_config ?? defaultConfigPathForMode(mode);
        runtime.lastMessage = `Loaded ${mode} state from local API at ${runtime.apiBase}.`;
    } catch (error) {
        applyState(structuredClone(sampleStates[mode]));
        runtime.apiConnected = false;
        runtime.sourceConfig = 'sample-state';
        runtime.lastMessage = `Local API unavailable, using sample ${mode} data instead.`;
    }

    if (runtime.apiConnected) {
        await loadStrategyFileCatalog({ silent: true });
    } else {
        closeLiveStateStream();
        resetStrategyFileCatalog();
        runtime.strategyFileCatalogStatus = 'error';
        runtime.strategyFileCatalogError = 'Local API is offline.';
    }

    if (mode === 'live' && runtime.apiConnected) {
        scheduleLiveRuntimePoll(1200);
        scheduleRecorderRuntimePoll(1200);
        openLiveStateStream();
    } else {
        closeLiveStateStream();
    }

    render();
}

function switchMode(mode) {
    if (uiState.pendingBacktestRunKey) {
        cancelBacktestRunWait();
    }
    if (mode !== 'live') {
        clearLiveRuntimePollTimer();
        clearRecorderRuntimePollTimer();
        closeLiveStateStream();
    }
    hydrateMode(mode).catch(() => {
        applyState(structuredClone(sampleStates[mode]));
        runtime.apiConnected = false;
        runtime.sourceConfig = 'sample-state';
        runtime.lastMessage = `Mode switched to ${mode} with fallback sample data.`;
        render();
    });
}

function setActiveSection(sectionName) {
    document.querySelectorAll('.nav__item').forEach((button) => {
        button.classList.toggle('nav__item--active', button.dataset.section === sectionName);
    });

    Object.entries(sectionMap).forEach(([name, element]) => {
        element.classList.toggle('hidden', name !== sectionName && sectionName !== 'overview');
    });

    pageTitle.textContent = sectionName.charAt(0).toUpperCase() + sectionName.slice(1);
}

function renderMetrics() {
    const totalInstruments = new Set(state.strategies.flatMap((item) => item.instruments.split(',').map((part) => part.trim()).filter(Boolean))).size;
    const modeLabel = state.mode === 'live' ? 'Live' : 'Backtest';
    const liveGuard = runtime.apiConnected ? 'Local API connected' : 'Sample fallback active';
    const cards = [
        { label: 'Mode', value: modeLabel, subtext: liveGuard },
        { label: 'Accounts', value: state.accounts.length, subtext: 'Multi-account orchestration' },
        { label: 'Strategies', value: state.strategies.length, subtext: 'DLL strategy plugins' },
        { label: 'Instruments', value: totalInstruments, subtext: 'Symbols routed to the engine' }
    ];

    metrics.innerHTML = cards.map((card) => `
        <article class="metric-card">
            <p class="metric-card__label">${escapeHtml(card.label)}</p>
            <p class="metric-card__value">${escapeHtml(card.value)}</p>
            <p class="metric-card__subtext">${escapeHtml(card.subtext)}</p>
        </article>
    `).join('');
}

function createField(labelText, value, onInput, fullWidth = false, inputType = 'text', focusKey = '') {
    const wrapper = document.createElement('div');
    wrapper.className = `field${fullWidth ? ' field--full' : ''}`;

    const label = document.createElement('label');
    label.textContent = labelText;

    const input = document.createElement('input');
    input.type = inputType;
    input.value = value ?? '';
    if (focusKey) {
        input.dataset.focusKey = focusKey;
    }
    input.addEventListener('input', (event) => onInput(event.target.value));

    wrapper.append(label, input);
    return wrapper;
}

function createSelectField(labelText, value, options, onInput, fullWidth = false, focusKey = '') {
    const wrapper = document.createElement('div');
    wrapper.className = `field${fullWidth ? ' field--full' : ''}`;

    const label = document.createElement('label');
    label.textContent = labelText;

    const select = document.createElement('select');
    if (focusKey) {
        select.dataset.focusKey = focusKey;
    }
    options.forEach((optionValue) => {
        const option = document.createElement('option');
        option.value = optionValue;
        option.textContent = optionValue;
        option.selected = optionValue === value;
        select.append(option);
    });
    select.addEventListener('change', (event) => {
        allowFocusedSelectRenderBriefly();
        onInput(event.target.value);
    });

    wrapper.append(label, select);
    return wrapper;
}

function isBooleanEditorOptions(options) {
    return Array.isArray(options)
        && options.length === 2
        && options.includes('true')
        && options.includes('false');
}

function createBooleanToggleField(labelText, value, onInput, fullWidth = false, focusKey = '') {
    const wrapper = document.createElement('div');
    wrapper.className = `field${fullWidth ? ' field--full' : ''}`;

    const label = document.createElement('label');
    label.textContent = labelText;

    const normalizedValue = String(value) === 'true' ? 'true' : 'false';
    const group = document.createElement('div');
    group.className = 'field-toggle';
    group.setAttribute('role', 'group');
    group.setAttribute('aria-label', labelText);
    if (focusKey) {
        group.dataset.focusKey = focusKey;
    }

    ['true', 'false'].forEach((optionValue) => {
        const button = document.createElement('button');
        button.type = 'button';
        button.className = `field-toggle__button${optionValue === normalizedValue ? ' field-toggle__button--active' : ''}`;
        button.textContent = optionValue;
        button.setAttribute('aria-pressed', optionValue === normalizedValue ? 'true' : 'false');
        button.addEventListener('click', () => {
            onInput(optionValue);
        });
        group.append(button);
    });

    wrapper.append(label, group);
    return wrapper;
}

function createReadonlyField(labelText, value, fullWidth = false) {
    const wrapper = document.createElement('div');
    wrapper.className = `field${fullWidth ? ' field--full' : ''}`;

    const label = document.createElement('label');
    label.textContent = labelText;

    const readonly = document.createElement('div');
    readonly.className = 'field__readonly';
    readonly.textContent = value || 'Unassigned';

    wrapper.append(label, readonly);
    return wrapper;
}

function createSummaryCard(labelText, value) {
    const item = document.createElement('div');
    item.className = 'strategy-summary__item';

    const label = document.createElement('span');
    label.className = 'strategy-summary__label';
    label.textContent = labelText;

    const content = document.createElement('strong');
    content.textContent = value || 'Unassigned';

    item.append(label, content);
    return item;
}

function createActionField(labelText, value, onInput, actionText, onAction, {
    fullWidth = false,
    inputType = 'text',
    actionDisabled = false,
    focusKey = ''
} = {}) {
    const wrapper = document.createElement('div');
    wrapper.className = `field${fullWidth ? ' field--full' : ''}`;

    const label = document.createElement('label');
    label.textContent = labelText;

    const row = document.createElement('div');
    row.className = 'field__input-row';

    const input = document.createElement('input');
    input.type = inputType;
    input.value = value ?? '';
    if (focusKey) {
        input.dataset.focusKey = focusKey;
    }
    input.addEventListener('input', (event) => onInput(event.target.value));

    const actionButton = document.createElement('button');
    actionButton.type = 'button';
    actionButton.className = 'secondary-button field__action';
    actionButton.textContent = actionText;
    actionButton.disabled = actionDisabled;
    actionButton.addEventListener('click', onAction);

    row.append(input, actionButton);
    wrapper.append(label, row);
    return wrapper;
}

function captureFocusedFieldState() {
    const activeElement = document.activeElement;
    if (!(activeElement instanceof HTMLInputElement) && !(activeElement instanceof HTMLSelectElement) && !(activeElement instanceof HTMLTextAreaElement)) {
        return null;
    }

    const focusKey = activeElement.dataset.focusKey;
    if (!focusKey) {
        return null;
    }

    return {
        key: focusKey,
        selectionStart: typeof activeElement.selectionStart === 'number' ? activeElement.selectionStart : null,
        selectionEnd: typeof activeElement.selectionEnd === 'number' ? activeElement.selectionEnd : null
    };
}

function restoreFocusedFieldState(focusState) {
    if (!focusState?.key) {
        return;
    }

    const target = document.querySelector(`[data-focus-key="${focusState.key}"]`);
    if (!(target instanceof HTMLInputElement) && !(target instanceof HTMLSelectElement) && !(target instanceof HTMLTextAreaElement)) {
        return;
    }

    target.focus();
    if (typeof target.selectionStart === 'number' && focusState.selectionStart !== null) {
        const start = Math.min(focusState.selectionStart, target.value.length);
        const end = Math.min(focusState.selectionEnd ?? focusState.selectionStart, target.value.length);
        target.setSelectionRange(start, end);
    }
}

function focusedSelectShouldHoldRender() {
    return document.activeElement instanceof HTMLSelectElement
        && document.body.contains(document.activeElement)
        && !renderRuntime.allowFocusedSelectRender;
}

function allowFocusedSelectRenderBriefly() {
    renderRuntime.allowFocusedSelectRender = true;
    if (renderRuntime.allowFocusedSelectRenderTimer !== null) {
        window.clearTimeout(renderRuntime.allowFocusedSelectRenderTimer);
    }
    renderRuntime.allowFocusedSelectRenderTimer = window.setTimeout(() => {
        renderRuntime.allowFocusedSelectRender = false;
        renderRuntime.allowFocusedSelectRenderTimer = null;
        flushDeferredSelectRender();
    }, 300);
}

function deferRenderUntilSelectSettles() {
    renderRuntime.deferredWhileSelectActive = true;
}

function flushDeferredSelectRender() {
    if (!renderRuntime.deferredWhileSelectActive || focusedSelectShouldHoldRender()) {
        return;
    }
    renderRuntime.deferredWhileSelectActive = false;
    render({ immediate: true });
}

function ensureStrategyUiState(strategy) {
    if (strategy.__runtimeStatus !== 'running' && strategy.__runtimeStatus !== 'stopped' && strategy.__runtimeStatus !== 'failed') {
        strategy.__runtimeStatus = 'stopped';
    }
    if (typeof strategy.__runtimeError !== 'string') {
        strategy.__runtimeError = '';
    }
}

function normalizeStrategyAccounts(strategy) {
    const explicitAccounts = Array.isArray(strategy.accounts) ? strategy.accounts : [];
    const legacyAccount = typeof strategy.account === 'string' ? strategy.account : '';
    const combined = [
        ...explicitAccounts,
        ...(explicitAccounts.length === 0 && legacyAccount ? legacyAccount.split(',') : [])
    ];

    strategy.accounts = Array.from(new Set(
        combined
            .map((value) => String(value).trim())
            .filter(Boolean)
    ));
}

function normalizeStrategyRuntimeDetails(strategy) {
    const details = strategy.runtime_details && typeof strategy.runtime_details === 'object'
        ? strategy.runtime_details
        : {};

    const normalizePosition = (position, accountId) => {
        const legacyLongQuantity = Number(position?.long_quantity ?? (Number(position?.net ?? 0) > 0 ? Number(position.net) : 0));
        const legacyShortQuantity = Number(position?.short_quantity ?? (Number(position?.net ?? 0) < 0 ? Math.abs(Number(position.net)) : 0));
        const longTodayQuantity = Number(position?.long_today_quantity ?? 0);
        const longYesterdayQuantity = Number(position?.long_yesterday_quantity ?? (longTodayQuantity === 0 ? legacyLongQuantity : Math.max(legacyLongQuantity - longTodayQuantity, 0)));
        const shortTodayQuantity = Number(position?.short_today_quantity ?? 0);
        const shortYesterdayQuantity = Number(position?.short_yesterday_quantity ?? (shortTodayQuantity === 0 ? legacyShortQuantity : Math.max(legacyShortQuantity - shortTodayQuantity, 0)));
        const longQuantity = Number.isFinite(legacyLongQuantity) && legacyLongQuantity > 0
            ? legacyLongQuantity
            : (longTodayQuantity + longYesterdayQuantity);
        const shortQuantity = Number.isFinite(legacyShortQuantity) && legacyShortQuantity > 0
            ? legacyShortQuantity
            : (shortTodayQuantity + shortYesterdayQuantity);
        const longAveragePrice = position?.long_average_price ?? (longQuantity > 0 && shortQuantity === 0 ? position?.average_price : '0.00');
        const shortAveragePrice = position?.short_average_price ?? (shortQuantity > 0 && longQuantity === 0 ? position?.average_price : '0.00');
        const net = Number.isFinite(Number(position?.net))
            ? Number(position.net)
            : (longQuantity - shortQuantity);
        const averagePrice = position?.average_price ?? ((longQuantity > 0 && shortQuantity === 0) || (shortQuantity > 0 && longQuantity === 0)
            ? (longQuantity > 0 ? longAveragePrice : shortAveragePrice)
            : '0.00');

        return {
            instrument: position?.instrument || '-',
            account_id: position?.account_id || accountId,
            strategy_id: position?.strategy_id || strategy.id,
            long_today_quantity: longTodayQuantity,
            long_yesterday_quantity: longYesterdayQuantity,
            long_quantity: longQuantity,
            long_average_price: longAveragePrice,
            short_today_quantity: shortTodayQuantity,
            short_yesterday_quantity: shortYesterdayQuantity,
            short_quantity: shortQuantity,
            short_average_price: shortAveragePrice,
            net,
            average_price: averagePrice
        };
    };

    const normalizedDetails = {};
    Object.entries(details).forEach(([accountId, value]) => {
        const openedOrders = Array.isArray(value?.opened_orders) ? value.opened_orders : [];
        const closedOrders = Array.isArray(value?.closed_orders) ? value.closed_orders : [];
        normalizedDetails[accountId] = {
            detail_level: normalizeBacktestDetailLevel(value?.detail_level),
            positions: Array.isArray(value?.positions) ? value.positions.map((position) => normalizePosition(position, accountId)) : [],
            opened_orders: openedOrders,
            closed_orders: closedOrders,
            opened_order_count: getRuntimeDetailCount(value, 'opened_order_count', openedOrders.length),
            closed_order_count: getRuntimeDetailCount(value, 'closed_order_count', closedOrders.length),
            filled_trade_count: getRuntimeDetailFilledTradeCount({ ...value, closed_orders: closedOrders }),
            connection_status_known: Boolean(value?.connection_status_known),
            trader_connected: Boolean(value?.trader_connected),
            market_data_connected: Boolean(value?.market_data_connected),
            warnings: Array.isArray(value?.warnings) ? value.warnings : []
        };
    });

    strategy.runtime_details = normalizedDetails;
}

function stabilizeLiveRuntimeWarnings(nextState) {
    return nextState;
}

function strategyAccountsText(strategy) {
    return strategy.accounts.length > 0 ? strategy.accounts.join(', ') : 'Unassigned';
}

function normalizePathForComparison(rawPath) {
    const normalized = String(rawPath ?? '').trim().replaceAll('\\', '/');
    if (!normalized) {
        return '';
    }

    const hasDrivePrefix = /^[A-Za-z]:\//.test(normalized);
    const isAbsolutePath = hasDrivePrefix || normalized.startsWith('/');
    const drivePrefix = hasDrivePrefix ? normalized.slice(0, 2) : '';
    const pathBody = hasDrivePrefix
        ? normalized.slice(2)
        : (normalized.startsWith('/') ? normalized.slice(1) : normalized);
    const segments = pathBody.split('/').filter(Boolean);
    const resolvedSegments = [];

    segments.forEach((segment) => {
        if (segment === '.') {
            return;
        }

        if (segment === '..') {
            if (resolvedSegments.length > 0 && resolvedSegments.at(-1) !== '..') {
                resolvedSegments.pop();
            } else if (!isAbsolutePath) {
                resolvedSegments.push('..');
            }
            return;
        }

        resolvedSegments.push(segment);
    });

    const joined = resolvedSegments.join('/');
    if (hasDrivePrefix) {
        return joined ? `${drivePrefix}/${joined}` : `${drivePrefix}/`;
    }

    if (normalized.startsWith('/')) {
        return joined ? `/${joined}` : '/';
    }

    return joined;
}

function resolveStrategyDllPath(dllPath) {
    const normalized = normalizePathForComparison(dllPath);
    if (!normalized) {
        return '';
    }

    if (/^[A-Za-z]:\//.test(normalized) || normalized.startsWith('/')) {
        return normalized.toLowerCase();
    }

    const sourceConfig = normalizePathForComparison(runtime.sourceConfig);
    if (!sourceConfig || sourceConfig === 'sample-state' || (!/^[A-Za-z]:\//.test(sourceConfig) && !sourceConfig.startsWith('/'))) {
        return normalized.toLowerCase();
    }

    const configDirectoryIndex = sourceConfig.lastIndexOf('/');
    const configDirectory = configDirectoryIndex >= 0 ? sourceConfig.slice(0, configDirectoryIndex) : sourceConfig;
    return normalizePathForComparison(`${configDirectory}/${normalized}`).toLowerCase();
}

function strategyDllFileName(dllPath) {
    return String(dllPath ?? '')
        .split(/[\\/]/)
        .at(-1)
        ?.toLowerCase() || '';
}

function resolveStrategyCatalogEntryPath(entry) {
    return resolveStrategyDllPath(entry?.absolute_path || entry?.dll || entry?.filename || entry?.id);
}

function findMatchingStrategyCatalogEntry(dllPath) {
    const resolvedPath = resolveStrategyDllPath(dllPath);
    if (!resolvedPath || runtime.strategyFileCatalog.length === 0) {
        return null;
    }

    const directMatch = runtime.strategyFileCatalog.find((entry) => resolveStrategyCatalogEntryPath(entry) === resolvedPath);
    if (directMatch) {
        return directMatch;
    }

    if (!resolvedPath.includes('/build/')) {
        return null;
    }

    const fileName = strategyDllFileName(dllPath);
    if (!fileName) {
        return null;
    }

    return runtime.strategyFileCatalog.find((entry) => strategyDllFileName(entry?.filename || entry?.dll || entry?.absolute_path) === fileName) || null;
}

function canonicalizeStrategyDllPath(dllPath) {
    const trimmedPath = String(dllPath ?? '').trim();
    if (!trimmedPath) {
        return '';
    }

    const catalogEntry = findMatchingStrategyCatalogEntry(trimmedPath);
    return catalogEntry?.dll || trimmedPath;
}

function normalizeStateStrategyDllPaths() {
    if (!Array.isArray(state.strategies) || runtime.strategyFileCatalog.length === 0) {
        return {
            normalizedStrategyCount: 0,
            removedLegacyFieldCount: 0
        };
    }

    let normalizedCount = 0;
    let removedLegacyFieldCount = 0;
    state.strategies.forEach((strategy) => {
        const canonicalDllPath = canonicalizeStrategyDllPath(strategy.dll);
        if (canonicalDllPath && canonicalDllPath !== strategy.dll) {
            strategy.dll = canonicalDllPath;
            normalizedCount += 1;
        }

        removedLegacyFieldCount += pruneLegacyStrategyFields(strategy);
    });
    return {
        normalizedStrategyCount: normalizedCount,
        removedLegacyFieldCount
    };
}

function getStrategyParameterSchema(dllPath) {
    const catalogEntry = findMatchingStrategyCatalogEntry(dllPath);
    return Array.isArray(catalogEntry?.parameter_schema) ? catalogEntry.parameter_schema : [];
}

function getStrategySchemaEntry(dllPath, key) {
    return getStrategyParameterSchema(dllPath)
        .find((entry) => String(entry?.key ?? '').trim() === String(key ?? '').trim()) ?? null;
}

function getStrategySchemaFieldKeys(strategy) {
    return getStrategyParameterSchema(strategy?.dll)
        .map((entry) => String(entry?.key ?? '').trim())
        .filter(Boolean);
}

function shouldSuppressLegacyStrategyField(strategy, key) {
    if (!suppressedLegacySchemaFieldKeys.has(key)) {
        return false;
    }

    const schemaKeys = getStrategySchemaFieldKeys(strategy);
    return schemaKeys.length > 0 && !schemaKeys.includes(key);
}

function pruneLegacyStrategyFields(strategy) {
    let removedCount = 0;
    suppressedLegacySchemaFieldKeys.forEach((key) => {
        if (!shouldSuppressLegacyStrategyField(strategy, key)) {
            return;
        }

        if (!Object.prototype.hasOwnProperty.call(strategy, key)) {
            return;
        }

        delete strategy[key];
        removedCount += 1;
    });
    return removedCount;
}

function getStrategySchemaDefaultValue(dllPath, key) {
    const schemaEntry = getStrategyParameterSchema(dllPath)
        .find((entry) => String(entry?.key ?? '').trim() === key);
    if (!schemaEntry) {
        return '';
    }

    if (schemaEntry.default_value === undefined || schemaEntry.default_value === null) {
        return '';
    }

    return String(schemaEntry.default_value);
}

function getStrategyFieldValue(strategy, key) {
    const forcedValue = getForcedStrategyFieldValue(key);
    if (forcedValue !== null) {
        return forcedValue;
    }

    if (Object.prototype.hasOwnProperty.call(strategy, key)) {
        return strategy[key] ?? '';
    }

    return getStrategySchemaDefaultValue(strategy?.dll, key);
}

function getStrategyFieldEditorOptions(strategy, key) {
    const schemaEntry = getStrategySchemaEntry(strategy?.dll, key);
    const rawSchemaType = String(schemaEntry?.type ?? '').trim();
    const schemaType = rawSchemaType.toLowerCase();
    if (schemaType === 'bool' || schemaType === 'boolean') {
        return ['true', 'false'];
    }

    const enumMatch = rawSchemaType.match(/^(?:enum|select|choice)\s*:\s*(.+)$/i);
    if (enumMatch) {
        const options = enumMatch[1]
            .split(/[|,]/)
            .map((option) => option.trim())
            .filter(Boolean);
        if (options.length > 0) {
            return options;
        }
    }

    return null;
}

function buildStrategyParameterDefaults(dllPath) {
    const defaults = {};
    getStrategyParameterSchema(dllPath).forEach((entry) => {
        const key = String(entry?.key ?? '').trim();
        if (!key) {
            return;
        }

        const forcedValue = getForcedStrategyFieldValue(key);
        if (forcedValue !== null) {
            defaults[key] = forcedValue;
            return;
        }

        if (entry.default_value === undefined || entry.default_value === null || entry.default_value === '') {
            return;
        }
        defaults[key] = String(entry.default_value);
    });
    return defaults;
}

function getCatalogEntryConfiguredStrategies(entry) {
    const catalogPath = resolveStrategyCatalogEntryPath(entry);
    return state.strategies.filter((strategy) => resolveStrategyDllPath(strategy.dll) === catalogPath);
}

function isStrategyCatalogBacked(strategy) {
    return Boolean(findMatchingStrategyCatalogEntry(strategy?.dll));
}

function attachStrategyToAccount(strategy, accountId) {
    normalizeStrategyAccounts(strategy);
    if (!strategy.accounts.includes(accountId)) {
        strategy.accounts = [...strategy.accounts, accountId];
    }
    normalizeStrategyAccounts(strategy);
    ensureStrategyUiState(strategy);
    setLocalStrategiesDraft();
}

function createStrategyFromDllPath(dllPath, accountId = '') {
    const canonicalDllPath = canonicalizeStrategyDllPath(dllPath);
    const inferredDefaults = buildStrategyParameterDefaults(canonicalDllPath);
    const strategy = {
        id: nextUniqueStrategyId(strategyBaseName(dllPath)),
        dll: canonicalDllPath,
        accounts: accountId ? [accountId] : [],
        __runtimeStatus: 'stopped',
        __runtimeError: '',
        instruments: '',
        ...inferredDefaults,
        quantity: inferredDefaults.quantity ?? '1'
    };
    if (state.mode === 'backtest') {
        strategy.backtest_data_dir = state.backtest.data_dir ?? '';
    }
    ensureStrategyUiState(strategy);
    return strategy;
}

function createConfiguredStrategyFromDll(dllPath, accountId) {
    const normalizedPath = String(dllPath ?? '').trim();
    if (!normalizedPath) {
        return null;
    }

    const createdStrategy = createStrategyFromDllPath(normalizedPath, accountId);
    state.strategies.push(createdStrategy);
    setLocalStrategiesDraft();
    return {
        strategy: createdStrategy,
        created: true
    };
}

async function browseStrategyDll(accountId) {
    uiState.activeStrategyPickerAccount = accountId;
    uiState.pendingStrategyBrowseAccount = accountId;
    uiState.strategyManualPath = '';
    render();

    loadStrategyFileCatalog()
        .finally(() => {
            uiState.pendingStrategyBrowseAccount = null;
            render();
        });
}

function closeStrategyPicker() {
    uiState.activeStrategyPickerAccount = null;
    uiState.pendingStrategyBrowseAccount = null;
    uiState.strategyManualPath = '';
    render();
}

function queueAccountStrategyReveal(accountId, strategyId) {
    if (!accountId || !strategyId) {
        uiState.pendingAccountStrategyReveal = null;
        return;
    }

    uiState.pendingAccountStrategyReveal = {
        accountId,
        strategyId,
        attempts: 0
    };
}

function flashStrategyRevealTarget(target) {
    if (!(target instanceof HTMLElement) || typeof target.animate !== 'function') {
        return;
    }

    target.animate([
        {
            transform: 'translateY(0)',
            boxShadow: '0 0 0 0 rgba(103, 232, 249, 0)'
        },
        {
            transform: 'translateY(-2px)',
            boxShadow: '0 0 0 3px rgba(103, 232, 249, 0.28)'
        },
        {
            transform: 'translateY(0)',
            boxShadow: '0 0 0 0 rgba(103, 232, 249, 0)'
        }
    ], {
        duration: 1200,
        easing: 'ease-out'
    });
}

function revealPendingAccountStrategy() {
    const pending = uiState.pendingAccountStrategyReveal;
    if (!pending?.accountId || !pending?.strategyId) {
        return;
    }

    const strategyKey = getAccountStrategyKey(pending.accountId, pending.strategyId);
    const strategyTarget = Array.from(document.querySelectorAll('[data-account-strategy-key]'))
        .find((node) => node instanceof HTMLElement && node.dataset.accountStrategyKey === strategyKey) || null;
    const accountTarget = Array.from(document.querySelectorAll('[data-account-id]'))
        .find((node) => node instanceof HTMLElement && node.dataset.accountId === pending.accountId) || null;
    const target = strategyTarget || accountTarget;

    if (!(target instanceof HTMLElement)) {
        if ((pending.attempts ?? 0) >= 2) {
            uiState.pendingAccountStrategyReveal = null;
            return;
        }

        uiState.pendingAccountStrategyReveal = {
            ...pending,
            attempts: (pending.attempts ?? 0) + 1
        };
        window.requestAnimationFrame(revealPendingAccountStrategy);
        return;
    }

    uiState.pendingAccountStrategyReveal = null;
    target.scrollIntoView({
        behavior: 'smooth',
        block: 'start',
        inline: 'nearest'
    });
    flashStrategyRevealTarget(target);
}

async function runStrategyBacktest(strategyId, accountId, { detailLevel = 'summary' } = {}) {
    if (state.mode !== 'backtest') {
        await startLiveRuntime(strategyId, accountId);
        return;
    }

    const normalizedDetailLevel = normalizeBacktestDetailLevel(detailLevel);

    const runKey = getAccountStrategyKey(accountId, strategyId);
    uiState.pendingBacktestRunKey = runKey;

    const strategy = state.strategies.find((candidate) => candidate.id === strategyId);
    if (strategy) {
        strategy.__runtimeStatus = 'running';
        strategy.__runtimeError = '';
    }

    startBacktestProgress(strategyId, accountId, normalizedDetailLevel);
    runtime.pendingBacktestStrategyId = strategyId;
    runtime.pendingBacktestAccountId = accountId;
    runtime.pendingBacktestDetailLevel = normalizedDetailLevel;
    runtime.lastMessage = `Running ${formatBacktestDetailLabel(normalizedDetailLevel)} for the current config from strategy ${strategyId} on account ${accountId}...`;
    render();

    try {
        advanceBacktestProgress(`Saving config for ${strategyId} on ${accountId}…`, 12);
        await saveConfigToWorkspace();
        advanceBacktestProgress(`Starting backend ${formatBacktestDetailLabel(normalizedDetailLevel)} worker…`, 18);
        const query = buildModeQuery('backtest');
        query.set('detail', normalizedDetailLevel);
        const response = await fetch(`${API_BASE}/api/backtest-run?${query.toString()}`, {
            method: 'POST'
        });
        if (!response.ok) {
            throw new Error(`HTTP ${response.status}`);
        }

        const job = await response.json();
        if (!job.ok || !job.id) {
            throw new Error(job.message || 'Unable to start the backend backtest replay job');
        }

        runtime.pendingBacktestJobId = job.id;
        runtime.pendingBacktestDetailLevel = normalizeBacktestDetailLevel(job.detail_level ?? normalizedDetailLevel);
        runtime.lastMessage = runtime.pendingBacktestDetailLevel === 'summary'
            ? `Backtest summary replay is running for strategy ${strategyId} on account ${accountId}. This keeps the UI lighter by skipping replay chart bars and row-level order history.`
            : `Backtest full replay is running for strategy ${strategyId} on account ${accountId}. This can take time if the configured backtest data dir is large.`;
        updateBacktestProgressFromJob(job);
        render();
        scheduleBacktestJobPoll(200);
    } catch (error) {
        const detail = error instanceof Error ? error.message : String(error);
        const activeStrategy = state.strategies.find((candidate) => candidate.id === strategyId);
        if (activeStrategy) {
            activeStrategy.__runtimeStatus = 'failed';
            activeStrategy.__runtimeError = detail;
        }
        runtime.lastMessage = `Backtest run failed for strategy ${strategyId}: ${detail}.`;
        uiState.pendingBacktestRunKey = null;
        resetBacktestProgress();
        render();
    }
}

async function startLiveRuntime(strategyId, accountId) {
    if (state.mode !== 'live' || runtime.pendingLiveControlAction) {
        return;
    }

    const strategy = state.strategies.find((candidate) => candidate.id === strategyId);
    const runKey = getAccountStrategyKey(accountId, strategyId);
    const query = buildModeQuery('live');
    query.set('strategy_ids', strategyId);
    runtime.pendingLiveControlAction = true;
    runtime.pendingLiveControlKey = runKey;
    runtime.lastMessage = `Saving config and launching itrader.exe in live mode for strategy ${strategyId} on account ${accountId}...`;
    if (strategy) {
        strategy.__runtimeError = '';
        if (strategy.__runtimeStatus === 'failed') {
            strategy.__runtimeStatus = 'stopped';
        }
    }
    render();

    try {
        await saveConfigToWorkspace();
        const response = await fetch(`${API_BASE}/api/live-run?${query.toString()}`, {
            method: 'POST'
        });
        if (!response.ok) {
            const detail = await response.text();
            throw new Error(detail || `HTTP ${response.status}`);
        }

        const payload = await response.json();
        const liveRuntime = applyLiveRuntimePayload(payload);
        syncStrategyStatusesFromLiveRuntime(liveRuntime);
        if (!payload.ok || liveRuntime.status !== 'running') {
            const detail = payload.message || 'The live runtime did not stay running after launch.';
            if (strategy && (!payload.ok || (liveRuntime.config_matches_request && liveRuntime.status !== 'running'))) {
                strategy.__runtimeStatus = 'failed';
                strategy.__runtimeError = detail;
            } else if (strategy) {
                strategy.__runtimeStatus = 'stopped';
                strategy.__runtimeError = '';
            }
            runtime.lastMessage = detail;
            render();
            return;
        }

        runtime.lastMessage = payload.message || `Live runtime launched for ${runtime.sourceConfig}.`;
    } catch (error) {
        const detail = error instanceof Error ? error.message : String(error);
        if (strategy) {
            strategy.__runtimeStatus = 'failed';
            strategy.__runtimeError = detail;
        }
        runtime.lastMessage = `Live launch failed for strategy ${strategyId}: ${detail}.`;
    } finally {
        runtime.pendingLiveControlAction = false;
        runtime.pendingLiveControlKey = '';
        scheduleLiveRuntimePoll(500);
        render();
    }
}

async function saveStrategyPreset(strategyId, accountId) {
    const strategy = state.strategies.find((candidate) => candidate.id === strategyId);
    if (!strategy || uiState.pendingStrategyPresetSaveKey) {
        return;
    }

    const saveKey = getStrategyPresetSaveKey(accountId, strategyId);
    uiState.pendingStrategyPresetSaveKey = saveKey;
    uiState.strategyPresetSaveErrors.delete(saveKey);
    runtime.lastMessage = `Saving preset ${strategyId} to the ${state.mode} config...`;
    render();

    try {
        await postConfigToWorkspace();
        promoteStrategyPresetDraftToBaseline(strategyId);
        clearLocalStrategiesDraft();
        markStrategyPresetSaved(saveKey);
        render({ immediate: true });

        let refreshSucceeded = true;
        try {
            const payload = await fetchModeStatePayload(state.mode);
            applyState(normalizeIncomingState(payload, state.mode));
            runtime.apiConnected = payload.api?.connected ?? true;
            runtime.sourceConfig = payload.api?.source_config ?? runtime.sourceConfig;
            clearLocalStrategyFieldEdits(strategyId);
            markStrategyPresetSaved(saveKey);
        } catch (refreshError) {
            refreshSucceeded = false;
        }

        if (!refreshSucceeded || hasUnsavedStrategyPresetChanges(strategyId)) {
            runtime.lastMessage = `Saved preset ${strategyId}. Reloading the dashboard from the saved config...`;
            reloadDashboardAfterPresetSave(strategyId, saveKey);
            return;
        }

        runtime.lastMessage = `Saved preset ${strategyId}. The generated ${state.mode} INI now includes these changes.`;
    } catch (error) {
        const detail = error instanceof Error ? error.message : String(error);
        uiState.strategyPresetSaveErrors.set(saveKey, detail);
        runtime.lastMessage = `Unable to save preset ${strategyId}: ${detail}.`;
    } finally {
        uiState.pendingStrategyPresetSaveKey = '';
        render();
    }
}

async function stopLiveRuntime(strategyId, accountId) {
    if (state.mode !== 'live' || runtime.pendingLiveControlAction) {
        return;
    }

    const query = buildModeQuery('live');
    query.set('strategy_ids', strategyId);
    const runKey = getAccountStrategyKey(accountId, strategyId);
    runtime.pendingLiveControlAction = true;
    runtime.pendingLiveControlKey = runKey;
    runtime.lastMessage = `Requesting a live runtime stop for strategy ${strategyId} on account ${accountId}...`;
    render();

    try {
        const response = await fetch(`${API_BASE}/api/live-run-stop?${query.toString()}`, {
            method: 'POST'
        });
        if (!response.ok) {
            const detail = await response.text();
            throw new Error(detail || `HTTP ${response.status}`);
        }

        const payload = await response.json();
        const liveRuntime = applyLiveRuntimePayload(payload);
        syncStrategyStatusesFromLiveRuntime(liveRuntime);
        runtime.lastMessage = payload.message || (liveRuntime.status === 'running'
            ? 'Stop requested for the live runtime; waiting for itrader.exe to exit.'
            : 'Live runtime stopped.');

        if (payload.ok) {
            const strategy = state.strategies.find((candidate) => candidate.id === strategyId);
            if (strategy && !findLiveRuntimeInstanceForStrategy(strategy, accountId, { statuses: ['running'] })) {
                strategy.__runtimeStatus = 'stopped';
                strategy.__runtimeError = '';
            }
        }
    } catch (error) {
        const detail = error instanceof Error ? error.message : String(error);
        const strategy = state.strategies.find((candidate) => candidate.id === strategyId);
        if (strategy) {
            strategy.__runtimeStatus = 'failed';
            strategy.__runtimeError = detail;
        }
        runtime.lastMessage = `Live stop request failed for strategy ${strategyId}: ${detail}.`;
    } finally {
        runtime.pendingLiveControlAction = false;
        runtime.pendingLiveControlKey = '';
        scheduleLiveRuntimePoll(500);
        render();
    }
}

function getActiveStrategyPickerAccount() {
    const activeAccountId = uiState.activeStrategyPickerAccount;
    if (!activeAccountId) {
        return null;
    }

    const activeAccount = state.accounts.find((account) => account.id === activeAccountId);
    if (!activeAccount) {
        uiState.activeStrategyPickerAccount = null;
        return null;
    }

    return activeAccount;
}

function finalizeStrategySelection(result, accountId, sourceLabel) {
    if (!result) {
        runtime.lastMessage = `No strategy DLL was selected for account ${accountId}.`;
        render();
        return;
    }

    expandAccountStrategy(accountId, result.strategy.id);
    queueAccountStrategyReveal(accountId, result.strategy.id);
    runtime.lastMessage = result.created
        ? `Created preset ${result.strategy.id} from ${sourceLabel} for account ${accountId}. It is staged in the current draft and expanded below so you can edit it right away. Click "Save to workspace" to persist it to the config file.`
        : `Attached saved preset ${result.strategy.id} to account ${accountId} from ${sourceLabel}. The updated attachment is staged in the current draft and expanded below so you can edit it right away. Click "Save to workspace" to persist it to the config file.`;
    closeStrategyPicker();
}

function handleStrategyCatalogAttach(strategy, accountId) {
    attachStrategyToAccount(strategy, accountId);
    finalizeStrategySelection({ strategy, created: false }, accountId, strategy.id);
}

function handleStrategyDllAttach(dllPath, accountId, sourceLabel) {
    const result = createConfiguredStrategyFromDll(dllPath, accountId);
    finalizeStrategySelection(result, accountId, sourceLabel);
}

function createStrategyPickerItem(titleText, metaText, actionText, action, {
    secondaryActionText = '',
    secondaryAction = null,
    secondaryActionClassName = 'ghost-button'
} = {}) {
    const item = document.createElement('div');
    item.className = 'strategy-picker-item';

    const info = document.createElement('div');
    info.className = 'strategy-picker-item__info';

    const title = document.createElement('div');
    title.className = 'strategy-picker-item__title';
    title.textContent = titleText;

    const meta = document.createElement('div');
    meta.className = 'strategy-picker-item__meta';
    meta.textContent = metaText;

    info.append(title, meta);

    const actions = document.createElement('div');
    actions.className = 'strategy-picker-item__actions';

    if (secondaryActionText && typeof secondaryAction === 'function') {
        const secondaryButton = document.createElement('button');
        secondaryButton.type = 'button';
        secondaryButton.className = secondaryActionClassName;
        secondaryButton.textContent = secondaryActionText;
        secondaryButton.addEventListener('click', secondaryAction);
        actions.append(secondaryButton);
    }

    const actionButton = document.createElement('button');
    actionButton.type = 'button';
    actionButton.className = 'primary-button';
    actionButton.textContent = actionText;
    actionButton.addEventListener('click', action);
    actions.append(actionButton);

    item.append(info, actions);
    return item;
}

function createStrategyPickerSection(titleText, subtitleText) {
    const section = document.createElement('section');
    section.className = 'strategy-picker-section';

    const header = document.createElement('div');
    header.className = 'strategy-picker-section__header';

    const title = document.createElement('div');
    title.className = 'strategy-picker-section__title';
    title.textContent = titleText;

    const subtitle = document.createElement('div');
    subtitle.className = 'strategy-picker-section__subtitle';
    subtitle.textContent = subtitleText;

    const list = document.createElement('div');
    list.className = 'strategy-picker-list';

    header.append(title, subtitle);
    section.append(header, list);
    return { section, list };
}

function renderStrategyPickerModal() {
    const activeAccount = getActiveStrategyPickerAccount();
    if (!activeAccount) {
        strategyPickerModal.classList.add('hidden');
        strategyPickerModal.setAttribute('aria-hidden', 'true');
        strategyPickerTitle.textContent = 'Attach strategy';
        strategyPickerSubtitle.textContent = 'Choose a strategy from the automatic catalog, use an existing preset, or type a remote path manually.';
        strategyPickerBody.innerHTML = '';
        return;
    }

    strategyPickerModal.classList.remove('hidden');
    strategyPickerModal.setAttribute('aria-hidden', 'false');
    strategyPickerTitle.textContent = `Attach strategy to ${activeAccount.id}`;
    strategyPickerSubtitle.textContent = `Pick a strategy DLL discovered under ${runtime.strategyFileCatalogRoot}, use an existing preset, or paste a remote path manually.`;
    strategyPickerBody.innerHTML = '';

    const configuredSection = createStrategyPickerSection(
        'Saved strategy definitions',
        `These catalog-backed saved strategy definitions already exist in the current config and can be attached directly to ${activeAccount.id}.`
    );
    const availableStrategies = getAvailableStrategiesForAccount(activeAccount.id)
        .filter((strategy) => isStrategyCatalogBacked(strategy));
    if (runtime.strategyFileCatalogStatus === 'loading' && runtime.strategyFileCatalog.length === 0) {
        const loading = document.createElement('div');
        loading.className = 'strategy-picker-empty';
        loading.textContent = `Checking saved strategy definitions against ${runtime.strategyFileCatalogRoot}...`;
        configuredSection.list.append(loading);
    } else if (availableStrategies.length === 0) {
        const empty = document.createElement('div');
        empty.className = 'strategy-picker-empty';
        empty.textContent = `No other catalog-backed saved strategy definitions are available for ${activeAccount.id}.`;
        configuredSection.list.append(empty);
    } else {
        availableStrategies.forEach((strategy) => {
            configuredSection.list.append(createStrategyPickerItem(
                strategy.id,
                `${strategyAccountsText(strategy)} | ${strategy.instruments || 'No instruments'}`,
                'Use existing preset',
                () => handleStrategyCatalogAttach(strategy, activeAccount.id),
                strategy.accounts.length === 0
                    ? {
                        secondaryActionText: 'Delete',
                        secondaryActionClassName: 'ghost-button ghost-button--danger',
                        secondaryAction: () => {
                            if (!confirmStrategyPresetRemoval(strategy, activeAccount.id, true)) {
                                runtime.lastMessage = `Cancelled deleting preset ${strategy.id}.`;
                                render();
                                return;
                            }
                            removeStrategyById(strategy.id);
                            runtime.lastMessage = `Deleted unassigned saved strategy definition ${strategy.id}.`;
                            render();
                        }
                    }
                    : undefined
            ));
        });
    }
    strategyPickerBody.append(configuredSection.section);

    const workspaceSection = createStrategyPickerSection(
        'Strategy catalog',
        `Remote paths come from the local API scan of ${runtime.strategyFileCatalogRoot}. Selecting one creates a new configurable strategy instance for this account in the current draft. Click Save to workspace afterwards to persist it.`
    );
    if (runtime.strategyFileCatalogStatus === 'loading') {
        const loading = document.createElement('div');
        loading.className = 'strategy-picker-empty';
        loading.textContent = `Scanning ${runtime.strategyFileCatalogRoot}...`;
        workspaceSection.list.append(loading);
    } else if (runtime.strategyFileCatalog.length === 0) {
        const empty = document.createElement('div');
        empty.className = 'strategy-picker-empty';
        empty.textContent = runtime.strategyFileCatalogStatus === 'error'
            ? `Catalog scan failed: ${runtime.strategyFileCatalogError || 'unknown error'}`
            : `No strategy DLLs were found under ${runtime.strategyFileCatalogRoot}.`;
        workspaceSection.list.append(empty);
    } else {
        runtime.strategyFileCatalog.forEach((entry) => {
            const item = createStrategyPickerItem(
                entry.id || strategyBaseName(entry.dll),
                entry.dll || entry.absolute_path || 'Unknown remote path',
                'Create draft preset',
                () => handleStrategyDllAttach(entry.dll || entry.absolute_path || '', activeAccount.id, entry.filename || entry.dll || 'strategy catalog')
            );

            if (entry.absolute_path && entry.absolute_path !== entry.dll) {
                const path = document.createElement('div');
                path.className = 'strategy-picker-path';
                path.textContent = entry.absolute_path;
                item.querySelector('.strategy-picker-item__info').append(path);
            }

            workspaceSection.list.append(item);
        });
    }
    strategyPickerBody.append(workspaceSection.section);

    const manualSection = createStrategyPickerSection(
        'Manual remote path',
        `Use this when the DLL is outside ${runtime.strategyFileCatalogRoot} or not visible in the automatic scan.`
    );
    const manualWrapper = document.createElement('div');
    manualWrapper.className = 'strategy-picker-manual';

    const manualInput = document.createElement('input');
    manualInput.type = 'text';
    manualInput.placeholder = '../build/Debug/sample_strategy.dll';
    manualInput.value = uiState.strategyManualPath;
    manualInput.addEventListener('input', (event) => {
        uiState.strategyManualPath = event.target.value;
    });

    const manualActions = document.createElement('div');
    manualActions.className = 'strategy-picker-manual__actions';

    const attachButton = document.createElement('button');
    attachButton.type = 'button';
    attachButton.className = 'primary-button';
    attachButton.textContent = 'Attach from path';
    attachButton.addEventListener('click', () => {
        const dllPath = uiState.strategyManualPath.trim();
        if (!dllPath) {
            runtime.lastMessage = `Enter a remote path before attaching a strategy to account ${activeAccount.id}.`;
            render();
            return;
        }

        handleStrategyDllAttach(dllPath, activeAccount.id, dllPath);
    });

    const refreshButton = document.createElement('button');
    refreshButton.type = 'button';
    refreshButton.className = 'secondary-button';
    refreshButton.textContent = runtime.strategyFileCatalogStatus === 'loading' ? 'Scanning...' : 'Rescan DLLs';
    refreshButton.disabled = runtime.strategyFileCatalogStatus === 'loading';
    refreshButton.addEventListener('click', () => {
        uiState.pendingStrategyBrowseAccount = activeAccount.id;
        render();
        loadStrategyFileCatalog({ force: true }).finally(() => {
            uiState.pendingStrategyBrowseAccount = null;
            render();
        });
    });

    manualActions.append(attachButton, refreshButton);
    manualWrapper.append(manualInput, manualActions);
    manualSection.list.append(manualWrapper);
    strategyPickerBody.append(manualSection.section);
}

function getAccountStrategyKey(accountId, strategyId) {
    return `${accountId}::${strategyId}`;
}

function expandAccountStrategy(accountId, strategyId) {
    const key = getAccountStrategyKey(accountId, strategyId);
    uiState.expandedAccountStrategies.add(key);
    if (!uiState.accountStrategyTabs.has(key)) {
        uiState.accountStrategyTabs.set(key, 'positions');
    }
}

function getStrategyParameterDraft(accountId, strategyId) {
    const key = getAccountStrategyKey(accountId, strategyId);
    if (!uiState.strategyParameterDrafts.has(key)) {
        uiState.strategyParameterDrafts.set(key, {
            name: '',
            value: ''
        });
    }

    return uiState.strategyParameterDrafts.get(key);
}

function setStrategyParameterDraft(accountId, strategyId, nextDraft) {
    uiState.strategyParameterDrafts.set(getAccountStrategyKey(accountId, strategyId), {
        name: nextDraft.name ?? '',
        value: nextDraft.value ?? ''
    });
}

function clearStrategyParameterDraft(accountId, strategyId) {
    uiState.strategyParameterDrafts.delete(getAccountStrategyKey(accountId, strategyId));
}

function isAccountStrategyExpanded(accountId, strategyId) {
    return uiState.expandedAccountStrategies.has(getAccountStrategyKey(accountId, strategyId));
}

function toggleAccountStrategyExpanded(accountId, strategyId) {
    const key = getAccountStrategyKey(accountId, strategyId);
    if (uiState.expandedAccountStrategies.has(key)) {
        uiState.expandedAccountStrategies.delete(key);
        return;
    }

    uiState.expandedAccountStrategies.add(key);
    if (!uiState.accountStrategyTabs.has(key)) {
        uiState.accountStrategyTabs.set(key, 'positions');
    }
}

function getActiveAccountStrategyTab(accountId, strategyId) {
    const key = getAccountStrategyKey(accountId, strategyId);
    if (!uiState.accountStrategyTabs.has(key)) {
        uiState.accountStrategyTabs.set(key, 'positions');
    }

    return uiState.accountStrategyTabs.get(key);
}

function setActiveAccountStrategyTab(accountId, strategyId, tabId) {
    uiState.accountStrategyTabs.set(getAccountStrategyKey(accountId, strategyId), tabId);
}

function getActiveAccountSummaryTab(accountId) {
    if (!uiState.accountSummaryTabs.has(accountId)) {
        uiState.accountSummaryTabs.set(accountId, 'positions');
    }

    return uiState.accountSummaryTabs.get(accountId);
}

function setActiveAccountSummaryTab(accountId, tabId) {
    uiState.accountSummaryTabs.set(accountId, tabId);
}

function moveAccountSummaryTab(previousAccountId, nextAccountId) {
    if (!previousAccountId || previousAccountId === nextAccountId) {
        return;
    }

    const activeTab = uiState.accountSummaryTabs.get(previousAccountId);
    uiState.accountSummaryTabs.delete(previousAccountId);
    if (activeTab && nextAccountId) {
        uiState.accountSummaryTabs.set(nextAccountId, activeTab);
    }
}

function clearAccountStrategyUiState(strategyId, accountId) {
    const directKey = accountId ? getAccountStrategyKey(accountId, strategyId) : null;
    if (directKey) {
        uiState.expandedAccountStrategies.delete(directKey);
        uiState.accountStrategyTabs.delete(directKey);
        uiState.strategyParameterDrafts.delete(directKey);
    }

    Array.from(uiState.expandedAccountStrategies).forEach((key) => {
        if (key.endsWith(`::${strategyId}`)) {
            uiState.expandedAccountStrategies.delete(key);
        }
    });

    Array.from(uiState.accountStrategyTabs.keys()).forEach((key) => {
        if (key.endsWith(`::${strategyId}`)) {
            uiState.accountStrategyTabs.delete(key);
        }
    });

    Array.from(uiState.strategyParameterDrafts.keys()).forEach((key) => {
        if (key.endsWith(`::${strategyId}`)) {
            uiState.strategyParameterDrafts.delete(key);
        }
    });
}

function isStrategyCollapsed(strategyId) {
    return uiState.collapsedStrategies.has(strategyId);
}

function isLiveInventoryCollapsed(accountId) {
    return !uiState.expandedLiveInventoryAccounts.has(accountId);
}

function setLiveInventoryCollapsed(accountId, collapsed) {
    if (collapsed) {
        uiState.expandedLiveInventoryAccounts.delete(accountId);
        return;
    }

    uiState.expandedLiveInventoryAccounts.add(accountId);
}

function getAssignedStrategies(accountId) {
    return state.strategies.filter((strategy) => strategy.accounts.includes(accountId));
}

function getAvailableStrategiesForAccount(accountId) {
    return state.strategies.filter((strategy) => !strategy.accounts.includes(accountId));
}

function removeStrategyById(strategyId) {
    const index = state.strategies.findIndex((strategy) => strategy.id === strategyId);
    if (index >= 0) {
        clearAccountStrategyUiState(strategyId);
        state.strategies.splice(index, 1);
        setLocalStrategiesDraft();
    }
}

function createStrategyDetailTable(columns, rows, emptyMessage) {
    const wrapper = document.createElement('div');
    wrapper.className = 'strategy-detail-table';

    if (!rows.length) {
        const empty = document.createElement('div');
        empty.className = 'strategy-detail-empty';
        empty.textContent = emptyMessage;
        wrapper.append(empty);
        return wrapper;
    }

    const table = document.createElement('table');

    const head = document.createElement('thead');
    const headRow = document.createElement('tr');
    columns.forEach((column) => {
        const cell = document.createElement('th');
        cell.textContent = column;
        headRow.append(cell);
    });
    head.append(headRow);

    const body = document.createElement('tbody');
    rows.forEach((row) => {
        const bodyRow = document.createElement('tr');
        row.forEach((value) => {
            const cell = document.createElement('td');
            cell.textContent = value;
            bodyRow.append(cell);
        });
        body.append(bodyRow);
    });

    table.append(head, body);
    wrapper.append(table);
    return wrapper;
}

function formatDisplayNumber(value) {
    if (value === null || value === undefined || value === '') {
        return '-';
    }

    const numeric = Number(value);
    if (Number.isFinite(numeric)) {
        return numeric.toFixed(2);
    }

    return String(value);
}

function parseFiniteNumber(value) {
    const numeric = Number(value);
    return Number.isFinite(numeric) ? numeric : null;
}

function formatSignedDisplayNumber(value) {
    const numeric = parseFiniteNumber(value);
    if (numeric === null) {
        return '-';
    }

    if (numeric > 0) {
        return `+${numeric.toFixed(2)}`;
    }

    return numeric.toFixed(2);
}

function formatPercent(value) {
    const numeric = parseFiniteNumber(value);
    if (numeric === null) {
        return '-';
    }

    if (numeric > 0) {
        return `+${numeric.toFixed(2)}%`;
    }

    return `${numeric.toFixed(2)}%`;
}

function formatDurationMs(value) {
    const milliseconds = Number(value ?? 0);
    if (!Number.isFinite(milliseconds) || milliseconds <= 0) {
        return '0.0s';
    }

    if (milliseconds < 1000) {
        return `${(milliseconds / 1000).toFixed(1)}s`;
    }

    const seconds = milliseconds / 1000;
    if (seconds < 60) {
        return `${seconds.toFixed(1)}s`;
    }

    const minutes = Math.floor(seconds / 60);
    const remainingSeconds = Math.round(seconds % 60);
    return `${minutes}m ${remainingSeconds}s`;
}

function formatCount(value) {
    const numeric = Number(value ?? 0);
    if (!Number.isFinite(numeric)) {
        return '0';
    }

    return Math.max(0, Math.round(numeric)).toLocaleString('en-US');
}

function normalizeBacktestDetailLevel(value) {
    return String(value ?? '').trim().toLowerCase() === 'summary' ? 'summary' : 'full';
}

function formatBacktestDetailLabel(value) {
    return normalizeBacktestDetailLevel(value) === 'summary' ? 'summary replay' : 'full replay';
}

function formatCountLabel(count, singularLabel, pluralLabel) {
    return `${formatCount(count)} ${count === 1 ? singularLabel : pluralLabel}`;
}

function resetBacktestCompletionNotice() {
    runtime.backtestCompletionVisible = false;
    runtime.backtestCompletionTitle = 'Backtesting Done';
    runtime.backtestCompletionMessage = '';
    runtime.backtestCompletionMeta = '';
}

function openBacktestCompletionNotice({
    title = 'Backtesting Done',
    message = 'Backtest replay completed successfully.',
    meta = ''
} = {}) {
    runtime.backtestCompletionVisible = true;
    runtime.backtestCompletionTitle = title;
    runtime.backtestCompletionMessage = message;
    runtime.backtestCompletionMeta = meta;
}

function closeBacktestCompletionNotice() {
    if (!runtime.backtestCompletionVisible) {
        return;
    }

    resetBacktestCompletionNotice();
    render();
}

function getRuntimeDetailCount(runtimeDetails, countKey, fallbackCount = 0) {
    const numeric = Number(runtimeDetails?.[countKey]);
    if (Number.isFinite(numeric) && numeric >= 0) {
        return Math.round(numeric);
    }

    const fallbackNumeric = Number(fallbackCount ?? 0);
    if (Number.isFinite(fallbackNumeric) && fallbackNumeric >= 0) {
        return Math.round(fallbackNumeric);
    }

    return 0;
}

function getRuntimeDetailFilledTradeCount(runtimeDetails) {
    const closedOrders = Array.isArray(runtimeDetails?.closed_orders) ? runtimeDetails.closed_orders : [];
    return getRuntimeDetailCount(
        runtimeDetails,
        'filled_trade_count',
        closedOrders.filter((order) => Number(order.filled_volume ?? 0) > 0).length
    );
}

function buildSummaryOnlyEmptyMessage(baseMessage, totalCount, singularLabel, pluralLabel) {
    if (totalCount <= 0) {
        return baseMessage;
    }

    return `${baseMessage} Summary replay kept counts for ${formatCountLabel(totalCount, singularLabel, pluralLabel)}. Click Run to inspect the row-level history.`;
}

function appendSummaryOnlyCountNote(panel, totalCount, visibleCount, singularLabel, pluralLabel) {
    const hiddenCount = Math.max(0, totalCount - visibleCount);
    if (hiddenCount <= 0 || visibleCount <= 0) {
        return;
    }

    const note = document.createElement('p');
    note.className = 'strategy-detail-note';
    note.textContent = `Summary replay kept counts for ${formatCountLabel(hiddenCount, singularLabel, pluralLabel)} beyond the ${formatCount(visibleCount)} visible row${visibleCount === 1 ? '' : 's'} here. Click Run to inspect the complete row-level history.`;
    panel.append(note);
}

function clearBacktestProgressTimer() {
    if (runtime.pendingBacktestProgressTimer !== null) {
        window.clearInterval(runtime.pendingBacktestProgressTimer);
        runtime.pendingBacktestProgressTimer = null;
    }
}

function clearBacktestPollTimer() {
    if (runtime.pendingBacktestPollTimer !== null) {
        window.clearTimeout(runtime.pendingBacktestPollTimer);
        runtime.pendingBacktestPollTimer = null;
    }
}

function describeBacktestPhase(phase) {
    switch (phase) {
    case 'queued':
        return 'Queued for replay...';
    case 'preparing_backtest_replay':
        return 'Preparing backtest replay...';
    case 'opening_backtest_files':
        return 'Opening backtest files...';
    case 'loading_backtest_ticks':
        return 'Loading backtest ticks...';
    case 'replaying_ticks':
        return 'Replaying AGTICK data and collecting fills...';
    case 'finalizing_snapshot':
        return 'Finalizing snapshot and performance metrics...';
    case 'cancelling':
        return 'Cancelling the backend replay...';
    case 'cancelled':
        return 'Backtest replay cancelled.';
    case 'completed':
        return 'Backtest replay completed.';
    case 'failed':
        return 'Backtest replay failed.';
    default:
        return 'Running backtest replay...';
    }
}

function updateBacktestProgressFromJob(job) {
    runtime.pendingBacktestJobId = job.id || runtime.pendingBacktestJobId;
    runtime.pendingBacktestDetailLevel = normalizeBacktestDetailLevel(job.detail_level ?? runtime.pendingBacktestDetailLevel);
    runtime.pendingBacktestStage = describeBacktestPhase(job.phase);
    runtime.pendingBacktestElapsedMs = runtime.pendingBacktestStartedAt > 0
        ? Date.now() - runtime.pendingBacktestStartedAt
        : runtime.pendingBacktestElapsedMs;

    const processedFiles = Number(job.processed_files ?? 0);
    const totalFiles = Number(job.total_files ?? 0);
    const processedTicks = Number(job.processed_ticks ?? 0);
    runtime.pendingBacktestProcessedFiles = processedFiles;
    runtime.pendingBacktestTotalFiles = totalFiles;
    runtime.pendingBacktestProcessedTicks = processedTicks;
    if (totalFiles > 0) {
        const fileProgress = 15 + (processedFiles / totalFiles) * 75;
        runtime.pendingBacktestProgress = Math.min(fileProgress, 94);
    }

    if (job.phase === 'finalizing_snapshot') {
        runtime.pendingBacktestProgress = Math.max(runtime.pendingBacktestProgress, 97);
    }

    if (job.status === 'completed') {
        runtime.pendingBacktestProgress = 100;
    }

    renderBacktestProgress();
}

async function fetchBacktestJobStatus(jobId) {
    const response = await fetch(`${API_BASE}/api/backtest-run?id=${encodeURIComponent(jobId)}`);
    if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
    }
    return response.json();
}

function scheduleBacktestJobPoll(delayMs = 500) {
    clearBacktestPollTimer();
    runtime.pendingBacktestPollTimer = window.setTimeout(() => {
        pollBacktestJobStatus().catch((error) => {
            const detail = error instanceof Error ? error.message : String(error);
            runtime.lastMessage = `Unable to poll the backtest replay job: ${detail}.`;
            const activeStrategy = state.strategies.find((candidate) => candidate.id === runtime.pendingBacktestStrategyId);
            if (activeStrategy) {
                activeStrategy.__runtimeStatus = 'failed';
                activeStrategy.__runtimeError = detail;
            }
            uiState.pendingBacktestRunKey = null;
            resetBacktestProgress();
            render();
        });
    }, delayMs);
}

async function pollBacktestJobStatus() {
    if (!runtime.pendingBacktestJobId) {
        return;
    }

    const job = await fetchBacktestJobStatus(runtime.pendingBacktestJobId);
    if (!job.ok) {
        throw new Error(job.message || 'Unknown backtest replay job error');
    }

    updateBacktestProgressFromJob(job);

    if (job.status === 'completed') {
        const payload = job.state;
        if (!payload) {
            throw new Error('Backtest replay completed without a state payload');
        }

        const completedDetailLevel = normalizeBacktestDetailLevel(job.detail_level ?? runtime.pendingBacktestDetailLevel);
        const completedStrategyId = runtime.pendingBacktestStrategyId;
        const completedAccountId = runtime.pendingBacktestAccountId;
        const completedElapsedMs = runtime.pendingBacktestElapsedMs;
        const completedFiles = Math.max(0, Number(job.total_files ?? runtime.pendingBacktestTotalFiles ?? 0));
        const completedTicks = Math.max(0, Number(job.processed_ticks ?? runtime.pendingBacktestProcessedTicks ?? 0));
        const completionMeta = [
            completedFiles > 0 ? formatCountLabel(completedFiles, 'file', 'files') : '',
            completedTicks > 0 ? formatCountLabel(completedTicks, 'tick', 'ticks') : '',
            completedElapsedMs > 0 ? `Elapsed ${formatDurationMs(completedElapsedMs)}` : ''
        ].filter(Boolean).join(' · ');

        applyState(normalizeIncomingState(payload, 'backtest'));
        runtime.apiConnected = payload.api?.connected ?? true;
        runtime.sourceConfig = payload.api?.source_config ?? defaultConfigPathForMode('backtest');
        normalizeStateStrategyDllPaths();
        finishBacktestProgress(`${formatBacktestDetailLabel(completedDetailLevel)} completed for ${completedStrategyId}.`);
        runtime.lastMessage = completedDetailLevel === 'summary'
            ? `Backtest summary replay completed for strategy ${completedStrategyId} on account ${completedAccountId}. Review the Backtest Performance panel and positions below. Use Run when you want replay chart bars and row-level order/trade history.`
            : `Backtest full replay completed for strategy ${completedStrategyId} on account ${completedAccountId}. Review the Backtest Performance panel, positions, trades, and chart markers below.`;
        openBacktestCompletionNotice({
            title: 'Backtesting Done',
            message: completedDetailLevel === 'summary'
                ? `Strategy ${completedStrategyId} on account ${completedAccountId} finished the summary replay successfully. The dashboard has been refreshed with the latest snapshot and performance counts.`
                : `Strategy ${completedStrategyId} on account ${completedAccountId} finished the full replay successfully. The dashboard has been refreshed with the latest chart markers, positions, and trade history.`,
            meta: completionMeta
        });
        uiState.pendingBacktestRunKey = null;
        resetBacktestProgress();
        render();
        return;
    }

    if (job.status === 'cancelled') {
        const activeStrategy = state.strategies.find((candidate) => candidate.id === runtime.pendingBacktestStrategyId);
        if (activeStrategy) {
            activeStrategy.__runtimeStatus = 'stopped';
            activeStrategy.__runtimeError = '';
        }
        runtime.lastMessage = 'Backtest replay cancelled on the backend.';
        uiState.pendingBacktestRunKey = null;
        resetBacktestProgress();
        render();
        return;
    }

    if (job.status === 'failed') {
        const activeStrategy = state.strategies.find((candidate) => candidate.id === runtime.pendingBacktestStrategyId);
        if (activeStrategy) {
            activeStrategy.__runtimeStatus = 'failed';
            activeStrategy.__runtimeError = job.error_message || 'unknown error';
        }
        runtime.lastMessage = `Backtest run failed for strategy ${runtime.pendingBacktestStrategyId}: ${job.error_message || 'unknown error'}.`;
        uiState.pendingBacktestRunKey = null;
        resetBacktestProgress();
        render();
        return;
    }

    scheduleBacktestJobPoll(500);
}

function renderBacktestProgress() {
    const isVisible = Boolean(uiState.pendingBacktestRunKey);
    if (!backtestProgress || !backtestProgressBar) {
        return;
    }

    backtestProgress.classList.toggle('hidden', !isVisible);
    if (!isVisible) {
        backtestProgressBar.style.width = '0%';
        if (backtestProgressFiles) {
            backtestProgressFiles.classList.add('hidden');
            backtestProgressFiles.textContent = '(0/0 files)';
        }
        return;
    }

    const progress = Math.max(0, Math.min(100, runtime.pendingBacktestProgress || 0));
    backtestProgressLabel.textContent = runtime.pendingBacktestStage || `Running ${formatBacktestDetailLabel(runtime.pendingBacktestDetailLevel)}…`;
    if (backtestProgressFiles) {
        const processedFiles = Math.max(0, Number(runtime.pendingBacktestProcessedFiles || 0));
        const totalFiles = Math.max(0, Number(runtime.pendingBacktestTotalFiles || 0));
        const processedTicks = Math.max(0, Number(runtime.pendingBacktestProcessedTicks || 0));
        if (totalFiles > 0) {
            backtestProgressFiles.classList.remove('hidden');
            const fileText = `${Math.min(processedFiles, totalFiles)}/${totalFiles} files`;
            const tickText = processedTicks > 0 ? ` · ${formatCount(processedTicks)} ticks` : '';
            backtestProgressFiles.textContent = `(${fileText}${tickText})`;
        } else if (processedTicks > 0) {
            backtestProgressFiles.classList.remove('hidden');
            backtestProgressFiles.textContent = `(${formatCount(processedTicks)} ticks)`;
        } else {
            backtestProgressFiles.classList.add('hidden');
            backtestProgressFiles.textContent = '(0/0 files)';
        }
    }
    backtestProgressValue.textContent = `${Math.round(progress)}%`;
    backtestProgressBar.style.width = `${progress}%`;
    backtestProgressElapsed.textContent = `Elapsed ${formatDurationMs(runtime.pendingBacktestElapsedMs)}`;
    cancelBacktestButton.disabled = !runtime.pendingBacktestJobId;
}

function startBacktestProgress(strategyId, accountId, detailLevel = 'summary') {
    clearBacktestProgressTimer();
    resetBacktestCompletionNotice();
    runtime.pendingBacktestDetailLevel = normalizeBacktestDetailLevel(detailLevel);
    runtime.pendingBacktestStartedAt = Date.now();
    runtime.pendingBacktestElapsedMs = 0;
    runtime.pendingBacktestProgress = 8;
    runtime.pendingBacktestProcessedFiles = 0;
    runtime.pendingBacktestTotalFiles = 0;
    runtime.pendingBacktestProcessedTicks = 0;
    runtime.pendingBacktestStage = `Preparing ${formatBacktestDetailLabel(runtime.pendingBacktestDetailLevel)} for ${strategyId} on ${accountId}…`;
    runtime.pendingBacktestProgressTimer = window.setInterval(() => {
        if (!uiState.pendingBacktestRunKey) {
            clearBacktestProgressTimer();
            return;
        }

        runtime.pendingBacktestElapsedMs = Date.now() - runtime.pendingBacktestStartedAt;
        if (runtime.pendingBacktestTotalFiles <= 0) {
            const elapsedSeconds = runtime.pendingBacktestElapsedMs / 1000;
            const easedProgress = Math.min(22, 10 + Math.sqrt(Math.max(elapsedSeconds, 0)) * 3 + Math.log1p(elapsedSeconds) * 4);
            runtime.pendingBacktestProgress = Math.max(runtime.pendingBacktestProgress, easedProgress);
        }
        renderBacktestProgress();
    }, 250);
    renderBacktestProgress();
}

function advanceBacktestProgress(stage, minimumProgress) {
    runtime.pendingBacktestStage = stage;
    runtime.pendingBacktestElapsedMs = runtime.pendingBacktestStartedAt > 0
        ? Date.now() - runtime.pendingBacktestStartedAt
        : runtime.pendingBacktestElapsedMs;
    runtime.pendingBacktestProgress = Math.max(runtime.pendingBacktestProgress, minimumProgress);
    renderBacktestProgress();
}

function finishBacktestProgress(stage) {
    runtime.pendingBacktestElapsedMs = runtime.pendingBacktestStartedAt > 0
        ? Date.now() - runtime.pendingBacktestStartedAt
        : runtime.pendingBacktestElapsedMs;
    runtime.pendingBacktestProgress = 100;
    runtime.pendingBacktestStage = stage;
    renderBacktestProgress();
    clearBacktestProgressTimer();
}

function resetBacktestProgress() {
    clearBacktestProgressTimer();
    clearBacktestPollTimer();
    runtime.pendingBacktestProgress = 0;
    runtime.pendingBacktestStage = '';
    runtime.pendingBacktestStartedAt = 0;
    runtime.pendingBacktestElapsedMs = 0;
    runtime.pendingBacktestProcessedFiles = 0;
    runtime.pendingBacktestTotalFiles = 0;
    runtime.pendingBacktestProcessedTicks = 0;
    runtime.pendingBacktestDetailLevel = 'summary';
    runtime.pendingBacktestAbortController = null;
    runtime.pendingBacktestJobId = '';
    runtime.pendingBacktestStrategyId = '';
    runtime.pendingBacktestAccountId = '';
    renderBacktestProgress();
}

async function cancelBacktestRunWait() {
    if (!runtime.pendingBacktestJobId) {
        runtime.lastMessage = 'No backend backtest replay job is currently active.';
        render();
        return;
    }

    try {
        runtime.pendingBacktestStage = 'Cancelling the backend replay...';
        renderBacktestProgress();
        const response = await fetch(`${API_BASE}/api/backtest-run-cancel?id=${encodeURIComponent(runtime.pendingBacktestJobId)}`, {
            method: 'POST'
        });
        if (!response.ok) {
            throw new Error(`HTTP ${response.status}`);
        }
        runtime.lastMessage = 'Backend cancellation requested. Waiting for the replay worker to stop...';
        scheduleBacktestJobPoll(200);
    } catch (error) {
        const detail = error instanceof Error ? error.message : String(error);
        runtime.lastMessage = `Unable to cancel the backend backtest replay: ${detail}.`;
        render();
    }
}

function collectBacktestPerformanceSummary() {
    if (state.mode !== 'backtest') {
        return {
            hasMetrics: false,
            aggregate: null,
            accounts: []
        };
    }

    const accounts = state.accounts.map((account) => {
        const assignedStrategies = getAssignedStrategies(account.id);
        const details = assignedStrategies.map((strategy) => strategy.runtime_details?.[account.id] ?? {
            detail_level: 'full',
            positions: [],
            opened_orders: [],
            closed_orders: [],
            opened_order_count: 0,
            closed_order_count: 0,
            filled_trade_count: 0,
            warnings: []
        });

        const positions = details.flatMap((detail) => detail.positions ?? []);
        const openedOrders = details.reduce((sum, detail) => sum + getRuntimeDetailCount(detail, 'opened_order_count', (detail.opened_orders ?? []).length), 0);
        const closedOrders = details.reduce((sum, detail) => sum + getRuntimeDetailCount(detail, 'closed_order_count', (detail.closed_orders ?? []).length), 0);
        const filledTrades = details.reduce((sum, detail) => sum + getRuntimeDetailFilledTradeCount(detail), 0);
        const initialCash = parseFiniteNumber(account.initial_cash) ?? 0;
        const realizedPnl = parseFiniteNumber(account.runtime_realized_pnl);
        const endingCash = parseFiniteNumber(account.runtime_cash);
        const returnPct = parseFiniteNumber(account.runtime_return_pct);

        return {
            accountId: account.id,
            initialCash,
            realizedPnl,
            endingCash,
            returnPct,
            openPositions: positions.filter((position) => Math.abs(Number(position.net ?? 0)) > 0).length,
            openedOrders,
            closedOrders,
            filledTrades,
            warnings: uniqueValues(details.flatMap((detail) => detail.warnings ?? []))
        };
    });

    const hasMetrics = accounts.some((account) => account.realizedPnl !== null || account.filledTrades > 0 || account.openPositions > 0);
    const aggregate = hasMetrics
        ? {
            initialCash: accounts.reduce((sum, account) => sum + account.initialCash, 0),
            realizedPnl: accounts.reduce((sum, account) => sum + (account.realizedPnl ?? 0), 0),
            endingCash: accounts.reduce((sum, account) => sum + (account.endingCash ?? account.initialCash), 0),
            returnPct: (() => {
                const totalInitialCash = accounts.reduce((sum, account) => sum + account.initialCash, 0);
                if (!Number.isFinite(totalInitialCash) || totalInitialCash === 0) {
                    return null;
                }
                return (accounts.reduce((sum, account) => sum + (account.realizedPnl ?? 0), 0) / totalInitialCash) * 100;
            })(),
            openPositions: accounts.reduce((sum, account) => sum + account.openPositions, 0),
            openedOrders: accounts.reduce((sum, account) => sum + account.openedOrders, 0),
            closedOrders: accounts.reduce((sum, account) => sum + account.closedOrders, 0),
            filledTrades: accounts.reduce((sum, account) => sum + account.filledTrades, 0)
        }
        : null;

    return {
        hasMetrics,
        aggregate,
        accounts
    };
}

function renderBacktestPerformance() {
    if (!backtestPerformanceSection || !backtestPerformanceContent) {
        return;
    }

    const isBacktest = state.mode === 'backtest';
    backtestPerformanceSection.classList.toggle('hidden', !isBacktest);
    backtestPerformanceContent.innerHTML = '';

    if (!isBacktest) {
        return;
    }

    if (uiState.pendingBacktestRunKey) {
        const pending = document.createElement('div');
        pending.className = 'backtest-performance__empty';
        pending.textContent = 'Backtest is running. The performance panel will refresh with the current run when the replay finishes.';
        backtestPerformanceContent.append(pending);
        return;
    }

    const performance = collectBacktestPerformanceSummary();
    if (!performance.hasMetrics || !performance.aggregate) {
        const empty = document.createElement('div');
        empty.className = 'backtest-performance__empty';
        empty.textContent = uiState.pendingBacktestRunKey
            ? 'Backtest is running. Metrics will appear here when the replay finishes.'
            : 'Click Run on an attached strategy to populate realized PnL, return, cash, and order/trade counts here.';
        backtestPerformanceContent.append(empty);
        return;
    }

    const aggregateGrid = document.createElement('div');
    aggregateGrid.className = 'backtest-performance__grid';
    [
        ['Total realized PnL', formatSignedDisplayNumber(performance.aggregate.realizedPnl), 'Across all backtest accounts'],
        ['Total return', formatPercent(performance.aggregate.returnPct), 'Relative to initial cash'],
        ['Ending cash', formatDisplayNumber(performance.aggregate.endingCash), 'Initial cash plus realized PnL'],
        ['Filled trades', String(performance.aggregate.filledTrades), `${performance.aggregate.closedOrders} closed orders tracked`]
    ].forEach(([label, value, subtext]) => {
        const card = document.createElement('article');
        card.className = 'performance-card';
        card.innerHTML = `
            <p class="performance-card__label">${escapeHtml(label)}</p>
            <p class="performance-card__value">${escapeHtml(value)}</p>
            <p class="performance-card__subtext">${escapeHtml(subtext)}</p>
        `;
        aggregateGrid.append(card);
    });
    backtestPerformanceContent.append(aggregateGrid);

    const accountGrid = document.createElement('div');
    accountGrid.className = 'backtest-performance__accounts';
    performance.accounts.forEach((account) => {
        const card = document.createElement('article');
        card.className = 'performance-account-card';
        card.innerHTML = `
            <div class="performance-account-card__header">
                <div class="performance-account-card__title">${escapeHtml(account.accountId)}</div>
                <div class="performance-account-card__pill">${escapeHtml(`${account.filledTrades} trade${account.filledTrades === 1 ? '' : 's'}`)}</div>
            </div>
            <div class="performance-account-card__grid">
                <div>
                    <div class="performance-account-card__label">Realized PnL</div>
                    <div class="performance-account-card__value">${escapeHtml(formatSignedDisplayNumber(account.realizedPnl))}</div>
                </div>
                <div>
                    <div class="performance-account-card__label">Return</div>
                    <div class="performance-account-card__value">${escapeHtml(formatPercent(account.returnPct))}</div>
                </div>
                <div>
                    <div class="performance-account-card__label">Ending cash</div>
                    <div class="performance-account-card__value">${escapeHtml(formatDisplayNumber(account.endingCash))}</div>
                </div>
                <div>
                    <div class="performance-account-card__label">Open positions</div>
                    <div class="performance-account-card__value">${escapeHtml(String(account.openPositions))}</div>
                </div>
            </div>
            <p class="performance-account-card__meta">Opened orders ${escapeHtml(String(account.openedOrders))} · Closed orders ${escapeHtml(String(account.closedOrders))}</p>
        `;

        if (account.warnings.length > 0) {
            const note = document.createElement('p');
            note.className = 'performance-account-card__warning';
            note.textContent = account.warnings[0];
            card.append(note);
        }

        accountGrid.append(card);
    });
    backtestPerformanceContent.append(accountGrid);
}

function buildAccountAggregate(accountId) {
    const aggregate = {
        strategy_ids: [],
        positions: [],
        opened_orders: [],
        closed_orders: [],
        trades: [],
        opened_order_count: 0,
        closed_order_count: 0,
        filled_trade_count: 0
    };

    getAssignedStrategies(accountId).forEach((strategy) => {
        normalizeStrategyRuntimeDetails(strategy);
        aggregate.strategy_ids.push(strategy.id);

        const runtimeDetails = strategy.runtime_details?.[accountId] ?? {
            detail_level: 'full',
            positions: [],
            opened_orders: [],
            closed_orders: [],
            opened_order_count: 0,
            closed_order_count: 0,
            filled_trade_count: 0,
            warnings: []
        };

        aggregate.opened_order_count += getRuntimeDetailCount(runtimeDetails, 'opened_order_count', (runtimeDetails.opened_orders ?? []).length);
        aggregate.closed_order_count += getRuntimeDetailCount(runtimeDetails, 'closed_order_count', (runtimeDetails.closed_orders ?? []).length);
        aggregate.filled_trade_count += getRuntimeDetailFilledTradeCount(runtimeDetails);

        aggregate.positions.push(...runtimeDetails.positions.map((position) => ({
            ...position,
            strategy_id: position.strategy_id || strategy.id,
            account_id: position.account_id || accountId
        })));

        aggregate.opened_orders.push(...runtimeDetails.opened_orders.map((order) => ({
            ...order,
            strategy_id: order.strategy_id || strategy.id,
            account_id: order.account_id || accountId
        })));

        const closedOrders = runtimeDetails.closed_orders.map((order) => ({
            ...order,
            strategy_id: order.strategy_id || strategy.id,
            account_id: order.account_id || accountId
        }));

        aggregate.closed_orders.push(...closedOrders);
        aggregate.trades.push(...closedOrders.filter((order) => Number(order.filled_volume ?? 0) > 0));
    });

    return aggregate;
}

function createAccountSummaryDetailPanel(tabId, accountId, aggregate) {
    const panel = document.createElement('div');
    panel.className = 'strategy-detail-panel__content';

    const openedOrderCount = getRuntimeDetailCount(aggregate, 'opened_order_count', aggregate.opened_orders.length);
    const closedOrderCount = getRuntimeDetailCount(aggregate, 'closed_order_count', aggregate.closed_orders.length);
    const filledTradeCount = getRuntimeDetailCount(aggregate, 'filled_trade_count', aggregate.trades.length);

    if (tabId === 'positions') {
        panel.append(createEditableAccountSummaryPositionsTable(accountId, aggregate.positions));
    } else if (tabId === 'opened-orders') {
        const rows = aggregate.opened_orders.map((order) => [
            order.strategy_id || '-',
            order.order_id || '-',
            order.instrument || '-',
            order.side || '-',
            order.status || '-',
            `${order.filled_volume ?? 0}/${order.requested_volume ?? 0}`,
            formatDisplayNumber(order.limit_price)
        ]);
        panel.append(createStrategyDetailTable(
            ['Strategy', 'Order ID', 'Instrument', 'Side', 'Status', 'Fill', 'Limit Px'],
            rows,
            buildSummaryOnlyEmptyMessage(
                `No opened orders are currently surfaced across the strategies attached to ${accountId}.`,
                openedOrderCount,
                'opened order',
                'opened orders'
            )
        ));
        appendSummaryOnlyCountNote(panel, openedOrderCount, rows.length, 'opened order', 'opened orders');
    } else if (tabId === 'trades') {
        const rows = aggregate.trades.map((order) => [
            order.strategy_id || '-',
            order.order_id || '-',
            order.instrument || '-',
            order.side || '-',
            order.offset || '-',
            String(order.filled_volume ?? '0'),
            formatDisplayNumber(order.filled_price),
            order.timestamp || '-'
        ]);
        panel.append(createStrategyDetailTable(
            ['Strategy', 'Trade ID', 'Instrument', 'Side', 'Offset', 'Qty', 'Price', 'Timestamp'],
            rows,
            buildSummaryOnlyEmptyMessage(
                `No trades are currently surfaced across the strategies attached to ${accountId}.`,
                filledTradeCount,
                'trade',
                'trades'
            )
        ));
        appendSummaryOnlyCountNote(panel, filledTradeCount, rows.length, 'trade', 'trades');
    } else {
        const rows = aggregate.closed_orders.map((order) => [
            order.strategy_id || '-',
            order.order_id || '-',
            order.instrument || '-',
            order.status || '-',
            `${order.filled_volume ?? 0}/${order.requested_volume ?? 0}`,
            formatDisplayNumber(order.filled_price),
            order.timestamp || '-'
        ]);
        panel.append(createStrategyDetailTable(
            ['Strategy', 'Order ID', 'Instrument', 'Result', 'Fill', 'Filled Px', 'Timestamp'],
            rows,
            buildSummaryOnlyEmptyMessage(
                `No closed orders are currently surfaced across the strategies attached to ${accountId}.`,
                closedOrderCount,
                'closed order',
                'closed orders'
            )
        ));
        appendSummaryOnlyCountNote(panel, closedOrderCount, rows.length, 'closed order', 'closed orders');
    }

    if (tabId === 'positions') {
        const note = document.createElement('p');
        note.className = 'strategy-detail-note';
        note.textContent = `This account-level view rolls up backend runtime snapshots from all ${aggregate.strategy_ids.length || 0} attached strategies on ${accountId}.`;
        panel.append(note);
    } else if (tabId === 'trades') {
        const note = document.createElement('p');
        note.className = 'strategy-detail-note';
        note.textContent = 'Trades are derived from closed order snapshots with non-zero filled quantity across all attached strategies.';
        panel.append(note);
    }

    return panel;
}

function createEditableAccountSummaryPositionsTable(accountId, positions) {
    const wrapper = document.createElement('div');
    wrapper.className = 'strategy-detail-table';

    if (!positions.length) {
        const empty = document.createElement('div');
        empty.className = 'strategy-detail-empty';
        empty.textContent = `No position snapshots are currently surfaced across the strategies attached to ${accountId}.`;
        wrapper.append(empty);
        return wrapper;
    }

    const table = document.createElement('table');
    const columns = ['Strategy', 'Instrument', 'Long T', 'Long Y', 'Long', 'Long Avg', 'Short T', 'Short Y', 'Short', 'Short Avg', 'Net'];
    const head = document.createElement('thead');
    const headRow = document.createElement('tr');
    columns.forEach((column) => {
        const cell = document.createElement('th');
        cell.textContent = column;
        headRow.append(cell);
    });
    head.append(headRow);

    const body = document.createElement('tbody');
    const appendTextCell = (row, value) => {
        const cell = document.createElement('td');
        cell.textContent = value || '-';
        row.append(cell);
    };
    const appendEditableCell = (row, position, globalIndex, fieldKey, fallbackValue, focusSuffix) => {
        const cell = document.createElement('td');
        const persisted = globalIndex >= 0 ? (getLiveInventoryState().persisted_positions[globalIndex] ?? {}) : {};
        const input = document.createElement('input');
        input.type = 'text';
        input.value = String(persisted[fieldKey] ?? fallbackValue ?? '0');
        input.dataset.focusKey = `account-summary-position:${accountId}:${position.strategy_id || ''}:${position.instrument || ''}:${focusSuffix}`;
        input.addEventListener('input', (event) => {
            if (globalIndex >= 0) {
                updatePersistedInventoryPositionField(globalIndex, fieldKey, event.target.value.trim());
            } else {
                updateOrCreatePersistedInventoryPositionField(position, accountId, fieldKey, event.target.value.trim());
            }
            render();
        });
        cell.append(input);
        row.append(cell);
    };

    positions.forEach((position) => {
        const globalIndex = findPersistedInventoryPositionIndex(accountId, position.strategy_id, position.instrument);
        const persisted = globalIndex >= 0 ? getLiveInventoryState().persisted_positions[globalIndex] : null;
        const source = persisted || position;
        const bodyRow = document.createElement('tr');
        appendTextCell(bodyRow, position.strategy_id || '-');
        appendTextCell(bodyRow, position.instrument || '-');
        appendEditableCell(bodyRow, position, globalIndex, 'long_today_quantity', String(source.long_today_quantity ?? '0'), 'long-today');
        appendEditableCell(bodyRow, position, globalIndex, 'long_yesterday_quantity', String(source.long_yesterday_quantity ?? '0'), 'long-yesterday');
        appendEditableCell(bodyRow, position, globalIndex, 'long_quantity', String(source.long_quantity ?? '0'), 'long');
        appendEditableCell(bodyRow, position, globalIndex, 'long_average_price', formatDisplayNumber(source.long_average_price), 'long-avg');
        appendEditableCell(bodyRow, position, globalIndex, 'short_today_quantity', String(source.short_today_quantity ?? '0'), 'short-today');
        appendEditableCell(bodyRow, position, globalIndex, 'short_yesterday_quantity', String(source.short_yesterday_quantity ?? '0'), 'short-yesterday');
        appendEditableCell(bodyRow, position, globalIndex, 'short_quantity', String(source.short_quantity ?? '0'), 'short');
        appendEditableCell(bodyRow, position, globalIndex, 'short_average_price', formatDisplayNumber(source.short_average_price), 'short-avg');
        appendTextCell(bodyRow, String(source.net ?? '0'));
        body.append(bodyRow);
    });

    table.append(head, body);
    wrapper.append(table);
    return wrapper;
}

function createAccountSummarySection(account) {
    const section = document.createElement('section');
    section.className = 'account-summary';

    const aggregate = buildAccountAggregate(account.id);
    const activeTab = getActiveAccountSummaryTab(account.id);
    const tabs = [
        { id: 'positions', label: 'Positions' },
        { id: 'trades', label: 'Trades' },
        { id: 'opened-orders', label: 'Opened orders' },
        { id: 'closed-orders', label: 'Closed orders' }
    ];

    const header = document.createElement('div');
    header.className = 'account-summary__header';

    const copy = document.createElement('div');
    copy.className = 'account-summary__copy';

    const title = document.createElement('div');
    title.className = 'account-summary__title';
    title.textContent = 'Account Summary';

    const subtitle = document.createElement('p');
    subtitle.className = 'account-summary__subtitle';
    subtitle.textContent = aggregate.strategy_ids.length > 0
        ? `Aggregated runtime details for ${aggregate.strategy_ids.length} attached ${aggregate.strategy_ids.length === 1 ? 'strategy' : 'strategies'}.`
        : 'Attach a strategy to start rolling up positions, trades, and orders here.';

    copy.append(title, subtitle);

    header.append(copy);
    section.append(header);

    const tabList = document.createElement('div');
    tabList.className = 'strategy-detail-tabs';
    tabList.setAttribute('role', 'tablist');
    tabList.setAttribute('aria-label', `${account.id} account summary details`);

    tabs.forEach((tab, index) => {
        const button = document.createElement('button');
        const isActive = activeTab === tab.id;
        const tabButtonId = `${account.id}-summary-${tab.id}-tab`;
        const panelId = `${account.id}-summary-${tab.id}-panel`;

        button.type = 'button';
        button.className = `strategy-detail-tab${isActive ? ' strategy-detail-tab--active' : ''}`;
        button.textContent = tab.label;
        button.id = tabButtonId;
        button.setAttribute('role', 'tab');
        button.setAttribute('aria-selected', isActive ? 'true' : 'false');
        button.setAttribute('aria-controls', panelId);
        button.tabIndex = isActive ? 0 : -1;
        button.addEventListener('click', () => {
            setActiveAccountSummaryTab(account.id, tab.id);
            render();
        });
        button.addEventListener('keydown', (event) => {
            if (!['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(event.key)) {
                return;
            }

            event.preventDefault();
            let nextIndex = index;
            if (event.key === 'ArrowRight') {
                nextIndex = (index + 1) % tabs.length;
            } else if (event.key === 'ArrowLeft') {
                nextIndex = (index - 1 + tabs.length) % tabs.length;
            } else if (event.key === 'Home') {
                nextIndex = 0;
            } else if (event.key === 'End') {
                nextIndex = tabs.length - 1;
            }

            setActiveAccountSummaryTab(account.id, tabs[nextIndex].id);
            render();
        });

        tabList.append(button);
    });

    section.append(tabList);

    tabs.forEach((tab) => {
        const isActive = activeTab === tab.id;
        const panel = document.createElement('div');
        panel.className = 'strategy-detail-panel__body';
        panel.id = `${account.id}-summary-${tab.id}-panel`;
        panel.setAttribute('role', 'tabpanel');
        panel.setAttribute('aria-labelledby', `${account.id}-summary-${tab.id}-tab`);
        panel.tabIndex = 0;
        panel.hidden = !isActive;
        panel.append(createAccountSummaryDetailPanel(tab.id, account.id, aggregate));
        section.append(panel);
    });

    return section;
}

function createLiveInventoryAdjustmentCard(adjustment, accountId, adjustmentIndex) {
    const card = document.createElement('section');
    card.className = 'strategy-detail-panel__body';

    const header = document.createElement('div');
    header.className = 'strategy-row__surface';

    const main = document.createElement('div');
    main.className = 'strategy-row__main';

    const title = document.createElement('div');
    title.className = 'strategy-row__title';
    title.textContent = adjustment.id || `adjustment_${adjustmentIndex + 1}`;

    const meta = document.createElement('div');
    meta.className = 'strategy-row__meta';
    meta.textContent = `${adjustment.strategy_id || 'external_manual'} · ${adjustment.instrument || 'No instrument'} · ${adjustment.reason_code || 'No reason code'}`;

    const compactMeta = document.createElement('div');
    compactMeta.className = 'strategy-row__compact-meta';

    const enabledPill = document.createElement('span');
    enabledPill.className = `strategy-pill${adjustment.enabled ? '' : ' strategy-pill--muted'}`;
    enabledPill.textContent = adjustment.enabled ? 'Enabled' : 'Disabled';

    const appliedPill = document.createElement('span');
    appliedPill.className = `strategy-status ${adjustment.applied ? 'strategy-status--running' : 'strategy-status--stopped'}`;
    appliedPill.textContent = adjustment.applied ? 'Applied' : 'Pending';

    compactMeta.append(enabledPill, appliedPill);
    main.append(title, meta, compactMeta);

    const actions = document.createElement('div');
    actions.className = 'strategy-row__actions';

    if (adjustment.applied) {
        const cloneButton = document.createElement('button');
        cloneButton.type = 'button';
        cloneButton.className = 'secondary-button';
        cloneButton.textContent = 'Clone as new';
        cloneButton.addEventListener('click', () => {
            const inventory = getLiveInventoryState();
            inventory.adjustments.push(cloneLiveInventoryAdjustment(adjustment));
            setLocalLiveInventoryDraftAdjustments(inventory.adjustments);
            runtime.lastMessage = `Cloned applied adjustment ${adjustment.id} to a new pending adjustment id.`;
            render();
        });
        actions.append(cloneButton);
    }

    const removeButton = document.createElement('button');
    removeButton.type = 'button';
    removeButton.className = 'ghost-button ghost-button--danger';
    removeButton.textContent = 'Delete';
    removeButton.addEventListener('click', () => {
        const inventory = getLiveInventoryState();
        inventory.adjustments = inventory.adjustments.filter((candidate) => candidate !== adjustment);
        setLocalLiveInventoryDraftAdjustments(inventory.adjustments);
        runtime.lastMessage = `Removed live inventory adjustment ${adjustment.id}.`;
        render();
    });
    actions.append(removeButton);

    header.append(main, actions);
    card.append(header);

    const content = document.createElement('div');
    content.className = 'strategy-detail-panel__content';

    const note = document.createElement('p');
    note.className = 'strategy-detail-note';
    note.textContent = adjustment.applied
        ? `This adjustment was already applied${adjustment.applied_at ? ` at ${adjustment.applied_at}` : ''}. Editing the same id only changes the file record; use Clone as new or change the id if you want a future live restart to apply a new delta.`
        : 'This adjustment is pending. Save the adjustment file, then restart the live runtime to let startup reconciliation apply it once.';
    content.append(note);

    const grid = document.createElement('div');
    grid.className = 'form-grid';

    const updateAdjustmentField = (key, nextValue) => {
        if (key === 'id' && adjustment.applied && String(nextValue).trim() !== adjustment.id) {
            adjustment.applied = false;
            adjustment.applied_at = '';
        }
        adjustment[key] = nextValue;
        setLocalLiveInventoryDraftAdjustments(getLiveInventoryState().adjustments);
        render();
    };

    grid.append(
        createReadonlyField('Account ID', accountId),
        createSelectField('Enabled', adjustment.enabled ? 'true' : 'false', ['true', 'false'], (nextValue) => {
            adjustment.enabled = nextValue === 'true';
            setLocalLiveInventoryDraftAdjustments(getLiveInventoryState().adjustments);
            render();
        }, false, `live-inventory:${accountId}:${adjustmentIndex}:enabled`),
        createField('Adjustment ID', adjustment.id, (nextValue) => updateAdjustmentField('id', nextValue.trim()), false, 'text', `live-inventory:${accountId}:${adjustmentIndex}:id`),
        createField('Strategy ID', adjustment.strategy_id, (nextValue) => updateAdjustmentField('strategy_id', nextValue.trim()), false, 'text', `live-inventory:${accountId}:${adjustmentIndex}:strategy_id`),
        createField('Instrument', adjustment.instrument, (nextValue) => updateAdjustmentField('instrument', nextValue.trim()), false, 'text', `live-inventory:${accountId}:${adjustmentIndex}:instrument`),
        createField('Exchange', adjustment.exchange, (nextValue) => updateAdjustmentField('exchange', nextValue.trim()), false, 'text', `live-inventory:${accountId}:${adjustmentIndex}:exchange`),
        createField('Operator ID', adjustment.operator_id, (nextValue) => updateAdjustmentField('operator_id', nextValue.trim()), false, 'text', `live-inventory:${accountId}:${adjustmentIndex}:operator_id`),
        createField('Reason Code', adjustment.reason_code, (nextValue) => updateAdjustmentField('reason_code', nextValue.trim()), false, 'text', `live-inventory:${accountId}:${adjustmentIndex}:reason_code`),
        createField('Reason Text', adjustment.reason_text, (nextValue) => updateAdjustmentField('reason_text', nextValue), true, 'text', `live-inventory:${accountId}:${adjustmentIndex}:reason_text`),
        createField('Long T Delta', adjustment.long_today_delta, (nextValue) => updateAdjustmentField('long_today_delta', nextValue.trim() || '0'), false, 'text', `live-inventory:${accountId}:${adjustmentIndex}:long_today_delta`),
        createField('Long T Avg Px', adjustment.long_today_average_price, (nextValue) => updateAdjustmentField('long_today_average_price', nextValue.trim()), false, 'text', `live-inventory:${accountId}:${adjustmentIndex}:long_today_average_price`),
        createField('Long Y Delta', adjustment.long_yesterday_delta, (nextValue) => updateAdjustmentField('long_yesterday_delta', nextValue.trim() || '0'), false, 'text', `live-inventory:${accountId}:${adjustmentIndex}:long_yesterday_delta`),
        createField('Long Y Avg Px', adjustment.long_yesterday_average_price, (nextValue) => updateAdjustmentField('long_yesterday_average_price', nextValue.trim()), false, 'text', `live-inventory:${accountId}:${adjustmentIndex}:long_yesterday_average_price`),
        createField('Short T Delta', adjustment.short_today_delta, (nextValue) => updateAdjustmentField('short_today_delta', nextValue.trim() || '0'), false, 'text', `live-inventory:${accountId}:${adjustmentIndex}:short_today_delta`),
        createField('Short T Avg Px', adjustment.short_today_average_price, (nextValue) => updateAdjustmentField('short_today_average_price', nextValue.trim()), false, 'text', `live-inventory:${accountId}:${adjustmentIndex}:short_today_average_price`),
        createField('Short Y Delta', adjustment.short_yesterday_delta, (nextValue) => updateAdjustmentField('short_yesterday_delta', nextValue.trim() || '0'), false, 'text', `live-inventory:${accountId}:${adjustmentIndex}:short_yesterday_delta`),
        createField('Short Y Avg Px', adjustment.short_yesterday_average_price, (nextValue) => updateAdjustmentField('short_yesterday_average_price', nextValue.trim()), false, 'text', `live-inventory:${accountId}:${adjustmentIndex}:short_yesterday_average_price`)
    );

    if (adjustment.applied_at) {
        grid.append(createReadonlyField('Applied At', adjustment.applied_at, true));
    }

    content.append(grid);
    card.append(content);
    return card;
}

function createPersistedInventoryStoreSection(accountId, positions) {
    const section = document.createElement('section');
    section.className = 'strategy-detail-panel__body';

    const content = document.createElement('div');
    content.className = 'strategy-detail-panel__content';

    const title = document.createElement('div');
    title.className = 'account-summary__title';
    title.textContent = 'Persisted Per-Strategy Inventory Store';

    const summary = summarizePersistedInventoryPositions(positions);
    const note = document.createElement('p');
    note.className = 'strategy-detail-note';
    note.textContent = summary.nonFlatCount > 0
        ? `This persisted store is non-flat for ${accountId}: long ${summary.longQuantity}, short ${summary.shortQuantity}, net ${summary.netQuantity}. Startup reconciliation will block if the broker currently reports no live positions for this account.`
        : `No non-flat persisted per-strategy inventory is currently stored for ${accountId}.`;

    content.append(title, note, createEditablePersistedInventoryTable(accountId, positions));
    section.append(content);
    return section;
}

function createEditablePersistedInventoryTable(accountId, positions) {
    const wrapper = document.createElement('div');
    wrapper.className = 'strategy-detail-table';

    if (!positions.length) {
        const empty = document.createElement('div');
        empty.className = 'strategy-detail-empty';
        empty.textContent = `No persisted per-strategy inventory positions are currently stored for ${accountId}.`;
        wrapper.append(empty);
        return wrapper;
    }

    const table = document.createElement('table');
    const columns = ['Store', 'Strategy', 'Instrument', 'Long T', 'Long Y', 'Long Avg', 'Long', 'Short T', 'Short Y', 'Short Avg', 'Short', 'Net'];

    const head = document.createElement('thead');
    const headRow = document.createElement('tr');
    columns.forEach((column) => {
        const cell = document.createElement('th');
        cell.textContent = column;
        headRow.append(cell);
    });
    head.append(headRow);

    const body = document.createElement('tbody');
    const inventory = getLiveInventoryState();
    const appendTextCell = (row, value) => {
        const cell = document.createElement('td');
        cell.textContent = value || '-';
        row.append(cell);
    };
    const appendInputCell = (row, position, globalIndex, fieldKey, focusSuffix) => {
        const cell = document.createElement('td');
        const input = document.createElement('input');
        input.type = 'text';
        input.value = String(position[fieldKey] ?? '');
        input.dataset.focusKey = `persisted-inventory:${accountId}:${globalIndex}:${focusSuffix}`;
        input.addEventListener('input', (event) => {
            updatePersistedInventoryPositionField(globalIndex, fieldKey, event.target.value.trim());
            render();
        });
        cell.append(input);
        row.append(cell);
    };

    positions.forEach((position) => {
        const globalIndex = inventory.persisted_positions.indexOf(position);
        const bodyRow = document.createElement('tr');
        appendTextCell(bodyRow, position.store_namespace || '-');
        appendTextCell(bodyRow, position.strategy_id || '-');
        appendTextCell(bodyRow, position.instrument || '-');
        appendInputCell(bodyRow, position, globalIndex, 'long_today_quantity', 'long-today');
        appendInputCell(bodyRow, position, globalIndex, 'long_yesterday_quantity', 'long-yesterday');
        appendInputCell(bodyRow, position, globalIndex, 'long_average_price', 'long-avg');
        appendInputCell(bodyRow, position, globalIndex, 'long_quantity', 'long');
        appendInputCell(bodyRow, position, globalIndex, 'short_today_quantity', 'short-today');
        appendInputCell(bodyRow, position, globalIndex, 'short_yesterday_quantity', 'short-yesterday');
        appendInputCell(bodyRow, position, globalIndex, 'short_average_price', 'short-avg');
        appendInputCell(bodyRow, position, globalIndex, 'short_quantity', 'short');
        appendTextCell(bodyRow, String(position.net ?? '0'));
        body.append(bodyRow);
    });

    table.append(head, body);
    wrapper.append(table);
    return wrapper;
}

function createLiveInventoryManagerSection(account) {
    if (state.mode !== 'live') {
        return null;
    }

    const inventory = getLiveInventoryState();
    const collapsed = isLiveInventoryCollapsed(account.id);
    const section = document.createElement('section');
    section.className = 'account-summary';

    const reconciliation = getLiveInventoryReconciliation(account.id);
    const accountAdjustments = getAccountLiveInventoryAdjustments(account.id);
    const persistedPositions = getAccountPersistedInventoryPositions(account.id);
    const persistedSummary = summarizePersistedInventoryPositions(persistedPositions);

    const header = document.createElement('div');
    header.className = 'account-summary__header';

    const copy = document.createElement('div');
    copy.className = 'account-summary__copy';

    const title = document.createElement('div');
    title.className = 'account-summary__title';
    title.textContent = 'Manual Inventory Adjustments';

    const subtitle = document.createElement('p');
    subtitle.className = 'account-summary__subtitle';
    if (!reconciliation) {
        subtitle.textContent = inventory.store_exists
            ? 'No reconciliation snapshot is available for this account yet.'
            : 'The live runtime has not produced a strategy inventory store yet. Save adjustments here, then start the live runtime to populate reconciliation snapshots.';
    } else if (reconciliation.aggregate_match) {
        subtitle.textContent = persistedSummary.nonFlatCount > 0
            ? `Last broker reconciliation matched for ${account.id}${reconciliation.broker_snapshot_timestamp ? ` (snapshot ${reconciliation.broker_snapshot_timestamp})` : ''}, but the persisted per-strategy store is currently non-flat: long ${persistedSummary.longQuantity}, short ${persistedSummary.shortQuantity}, net ${persistedSummary.netQuantity}.`
            : `Broker reconciliation currently matches for ${account.id}${reconciliation.broker_snapshot_timestamp ? ` (snapshot ${reconciliation.broker_snapshot_timestamp})` : ''}. Applied adjustment count: ${reconciliation.applied_adjustment_count}.`;
    } else {
        subtitle.textContent = reconciliation.mismatch_summary
            ? `Reconciliation mismatch: ${reconciliation.mismatch_summary}`
            : 'Broker reconciliation currently reports a mismatch for this account.';
    }

    copy.append(title, subtitle);

    const actions = document.createElement('div');
    actions.className = 'strategy-row__actions';

    const toggleButton = document.createElement('button');
    toggleButton.type = 'button';
    toggleButton.className = 'ghost-button';
    toggleButton.textContent = collapsed ? 'Unfold' : 'Fold';
    toggleButton.setAttribute('aria-expanded', collapsed ? 'false' : 'true');
    toggleButton.addEventListener('click', () => {
        setLiveInventoryCollapsed(account.id, !collapsed);
        render();
    });

    const statusPill = document.createElement('span');
    statusPill.className = `strategy-status ${reconciliation
        ? (reconciliation.aggregate_match ? 'strategy-status--running' : 'strategy-status--failed')
        : 'strategy-status--stopped'}`;
    statusPill.textContent = reconciliation
        ? (reconciliation.aggregate_match ? 'Reconciled' : 'Mismatch')
        : 'No snapshot';

    const reloadButton = document.createElement('button');
    reloadButton.type = 'button';
    reloadButton.className = 'secondary-button';
    reloadButton.textContent = runtime.pendingLiveInventoryRefresh ? 'Refreshing...' : 'Reload';
    reloadButton.disabled = runtime.pendingLiveInventoryRefresh || runtime.pendingLiveInventorySave;
    reloadButton.addEventListener('click', () => {
        refreshLiveInventoryFromApi();
    });

    const addButton = document.createElement('button');
    addButton.type = 'button';
    addButton.className = 'secondary-button';
    addButton.textContent = 'Add adjustment';
    addButton.disabled = runtime.pendingLiveInventorySave;
    addButton.addEventListener('click', () => {
        inventory.adjustments.push(createBlankLiveInventoryAdjustment(account.id));
        setLocalLiveInventoryDraftAdjustments(inventory.adjustments);
        setLiveInventoryCollapsed(account.id, false);
        runtime.lastMessage = `Added a new pending live inventory adjustment for account ${account.id}.`;
        render();
    });

    const saveButton = document.createElement('button');
    saveButton.type = 'button';
    saveButton.className = 'primary-button';
    const hasPersistedDraft = uiState.localPersistedInventoryDraft !== null;
    saveButton.textContent = runtime.pendingLiveInventorySave ? 'Saving...' : (hasPersistedDraft ? 'Save adjustments *' : 'Save adjustments');
    saveButton.disabled = runtime.pendingLiveInventorySave || runtime.pendingLiveInventoryRefresh;
    saveButton.addEventListener('click', () => {
        saveLiveInventoryAdjustments();
    });

    actions.append(toggleButton, statusPill, reloadButton, addButton, saveButton);
    header.append(copy, actions);
    section.append(header);

    if (collapsed) {
        const pendingAdjustments = accountAdjustments.filter((adjustment) => !adjustment.applied).length;
        const appliedAdjustments = accountAdjustments.filter((adjustment) => adjustment.applied).length;

        const summary = document.createElement('div');
        summary.className = 'strategy-summary';
        summary.append(
            createSummaryCard('Reconciliation', statusPill.textContent || 'No snapshot'),
            createSummaryCard('Persisted Net', persistedSummary.nonFlatCount > 0 ? String(persistedSummary.netQuantity) : 'Flat'),
            createSummaryCard('Adjustments', String(accountAdjustments.length)),
            createSummaryCard('Pending', String(pendingAdjustments)),
            createSummaryCard('Applied', String(appliedAdjustments))
        );
        section.append(summary);

        const note = document.createElement('p');
        note.className = 'strategy-detail-note';
        note.textContent = `${subtitle.textContent} Click Unfold to review or edit adjustment details.`;
        section.append(note);

        return section;
    }

    section.append(createPersistedInventoryStoreSection(account.id, persistedPositions));

    if (accountAdjustments.length === 0) {
        const empty = document.createElement('div');
        empty.className = 'strategy-detail-empty';
        empty.textContent = `No manual inventory adjustments are currently defined for ${account.id}. Add one if this account needs an external_manual bucket or a startup reconciliation override.`;
        section.append(empty);
    } else {
        accountAdjustments.forEach((adjustment, index) => {
            section.append(createLiveInventoryAdjustmentCard(adjustment, account.id, index));
        });
    }

    const pathNote = document.createElement('p');
    pathNote.className = 'strategy-detail-note';
    pathNote.textContent = inventory.adjustments_path
        ? `Dashboard saves to ${inventory.adjustments_path}. Reconciliation snapshots are read from ${inventory.store_path || 'runtime/strategy_inventory_store.ini'}.`
        : 'Dashboard saves to runtime/strategy_inventory_adjustments.ini.';
    section.append(pathNote);

    return section;
}

function createAccountStrategyDetailPanel(tabId, strategy, accountId) {
    const panel = document.createElement('div');
    panel.className = 'strategy-detail-panel__content';

    const runtimeDetails = strategy.runtime_details?.[accountId] ?? {
        detail_level: 'full',
        positions: [],
        opened_orders: [],
        closed_orders: [],
        opened_order_count: 0,
        closed_order_count: 0,
        filled_trade_count: 0,
        warnings: []
    };

    const openedOrderCount = getRuntimeDetailCount(runtimeDetails, 'opened_order_count', runtimeDetails.opened_orders.length);
    const closedOrderCount = getRuntimeDetailCount(runtimeDetails, 'closed_order_count', runtimeDetails.closed_orders.length);
    const filledTradeCount = getRuntimeDetailFilledTradeCount(runtimeDetails);

    if (tabId === 'positions') {
        const rows = runtimeDetails.positions.map((position) => [
            position.instrument || '-',
            String(position.long_today_quantity ?? '0'),
            String(position.long_yesterday_quantity ?? '0'),
            String(position.long_quantity ?? '0'),
            formatDisplayNumber(position.long_average_price),
            String(position.short_today_quantity ?? '0'),
            String(position.short_yesterday_quantity ?? '0'),
            String(position.short_quantity ?? '0'),
            formatDisplayNumber(position.short_average_price),
            String(position.net ?? '0'),
            position.account_id || accountId
        ]);
        panel.append(createStrategyDetailTable(
            ['Instrument', 'Long T', 'Long Y', 'Long', 'Long Avg', 'Short T', 'Short Y', 'Short', 'Short Avg', 'Net', 'Account'],
            rows,
            `No position snapshot is available yet for ${strategy.id} on ${accountId}.`
        ));
    } else if (tabId === 'opened-orders') {
        const rows = runtimeDetails.opened_orders.map((order) => [
            order.order_id || '-',
            order.instrument || '-',
            order.side || '-',
            order.status || '-',
            `${order.filled_volume ?? 0}/${order.requested_volume ?? 0}`,
            formatDisplayNumber(order.limit_price)
        ]);
        panel.append(createStrategyDetailTable(
            ['Order ID', 'Instrument', 'Side', 'Status', 'Fill', 'Limit Px'],
            rows,
            buildSummaryOnlyEmptyMessage(
                `No opened orders are currently surfaced for ${strategy.id} on ${accountId}.`,
                openedOrderCount,
                'opened order',
                'opened orders'
            )
        ));
        appendSummaryOnlyCountNote(panel, openedOrderCount, rows.length, 'opened order', 'opened orders');
    } else if (tabId === 'trades') {
        const tradeRows = runtimeDetails.closed_orders
            .filter((order) => Number(order.filled_volume ?? 0) > 0)
            .map((order) => [
                order.order_id || '-',
                order.instrument || '-',
                order.side || '-',
                order.offset || '-',
                String(order.filled_volume ?? '0'),
                formatDisplayNumber(order.filled_price),
                order.timestamp || '-'
            ]);
        panel.append(createStrategyDetailTable(
            ['Trade ID', 'Instrument', 'Side', 'Offset', 'Qty', 'Price', 'Timestamp'],
            tradeRows,
            buildSummaryOnlyEmptyMessage(
                `No trades are currently surfaced for ${strategy.id} on ${accountId}.`,
                filledTradeCount,
                'trade',
                'trades'
            )
        ));
        appendSummaryOnlyCountNote(panel, filledTradeCount, tradeRows.length, 'trade', 'trades');
    } else {
        const rows = runtimeDetails.closed_orders.map((order) => [
            order.order_id || '-',
            order.instrument || '-',
            order.status || '-',
            `${order.filled_volume ?? 0}/${order.requested_volume ?? 0}`,
            formatDisplayNumber(order.filled_price),
            order.timestamp || '-'
        ]);
        panel.append(createStrategyDetailTable(
            ['Order ID', 'Instrument', 'Result', 'Fill', 'Filled Px', 'Timestamp'],
            rows,
            buildSummaryOnlyEmptyMessage(
                `No closed orders are currently surfaced for ${strategy.id} on ${accountId}.`,
                closedOrderCount,
                'closed order',
                'closed orders'
            )
        ));
        appendSummaryOnlyCountNote(panel, closedOrderCount, rows.length, 'closed order', 'closed orders');
    }

    if (tabId === 'positions') {
        const note = document.createElement('p');
        note.className = 'strategy-detail-note';
        note.textContent = 'These rows are sourced from the backend runtime snapshot for this account-strategy attachment.';
        panel.append(note);
    } else if (tabId === 'trades') {
        const note = document.createElement('p');
        note.className = 'strategy-detail-note';
        note.textContent = 'Trades are derived from closed order snapshots with non-zero filled quantity for this account-strategy attachment.';
        panel.append(note);
    }

    return panel;
}

function createAccountStrategyConfigPanel(strategy, accountId, strategyIndex) {
    const panel = document.createElement('section');
    panel.className = 'strategy-detail-panel__body';

    const content = document.createElement('div');
    content.className = 'strategy-detail-panel__content';

    const note = document.createElement('p');
    note.className = 'strategy-detail-note';
    note.textContent = 'Configure this strategy instance here. Changes are staged in the generated INI preview; click Save preset to write them to the workspace config.';
    content.append(note);

    const grid = document.createElement('div');
    grid.className = 'form-grid';
    grid.append(
        createReadonlyField('Strategy ID', strategy.id || 'Unassigned', true),
        createReadonlyField('Remote Path', strategy.dll || 'Unassigned', true)
    );

    getStrategyFieldGroups(strategy)
        .filter(([key]) => key !== 'id' && key !== 'dll')
        .forEach(([key, label]) => {
            const fullWidth = key.includes('dir') || key === 'instruments' || key === '__runtimeStatus' || key === '__runtimeError';
            if (key === '__runtimeStatus') {
                grid.append(createReadonlyField(label, strategy[key] ?? 'stopped', true));
                return;
            }

            if (key === '__runtimeError') {
                grid.append(createReadonlyField(label, strategy[key] || 'None', true));
                return;
            }

            const focusKey = `account-strategy:${accountId}:${strategyIndex}:${key}`;
            const value = getStrategyFieldValue(strategy, key);
            const editorOptions = getStrategyFieldEditorOptions(strategy, key);
            const applyValue = (nextValue) => {
                setLocalStrategyFieldEdit(strategy.id, key, nextValue);
                render();
            };

            if (Array.isArray(editorOptions)) {
                const normalizedValue = editorOptions.includes(String(value)) ? String(value) : editorOptions[0];
                if (isBooleanEditorOptions(editorOptions)) {
                    grid.append(createBooleanToggleField(label, normalizedValue, applyValue, fullWidth, focusKey));
                    return;
                }
                grid.append(createSelectField(label, normalizedValue, editorOptions, applyValue, fullWidth, focusKey));
                return;
            }

            grid.append(createField(label, value, applyValue, fullWidth, 'text', focusKey));
        });

    const parameterDraft = getStrategyParameterDraft(accountId, strategy.id);
    const parameterField = document.createElement('div');
    parameterField.className = 'field field--full';

    const parameterLabel = document.createElement('label');
    parameterLabel.textContent = 'Add custom parameter';

    const parameterRow = document.createElement('div');
    parameterRow.className = 'field__input-row';

    const nameInput = document.createElement('input');
    nameInput.type = 'text';
    nameInput.placeholder = 'parameter_name';
    nameInput.value = parameterDraft.name;
    nameInput.dataset.focusKey = `account-strategy:${accountId}:${strategyIndex}:new-parameter-name`;
    nameInput.addEventListener('input', (event) => {
        setStrategyParameterDraft(accountId, strategy.id, {
            ...getStrategyParameterDraft(accountId, strategy.id),
            name: event.target.value
        });
    });

    const valueInput = document.createElement('input');
    valueInput.type = 'text';
    valueInput.placeholder = 'value';
    valueInput.value = parameterDraft.value;
    valueInput.dataset.focusKey = `account-strategy:${accountId}:${strategyIndex}:new-parameter-value`;
    valueInput.addEventListener('input', (event) => {
        setStrategyParameterDraft(accountId, strategy.id, {
            ...getStrategyParameterDraft(accountId, strategy.id),
            value: event.target.value
        });
    });

    const addButton = document.createElement('button');
    addButton.type = 'button';
    addButton.className = 'secondary-button field__action';
    addButton.textContent = 'Add';
    addButton.addEventListener('click', () => {
        const draft = getStrategyParameterDraft(accountId, strategy.id);
        const parameterName = draft.name.trim();
        const parameterValue = draft.value;

        if (!parameterName) {
            runtime.lastMessage = `Enter a parameter name before adding it to strategy ${strategy.id}.`;
            render();
            return;
        }

        if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(parameterName)) {
            runtime.lastMessage = `Parameter ${parameterName} is not a valid INI key. Use letters, numbers, and underscores only.`;
            render();
            return;
        }

        if (hiddenStrategyFieldKeys.has(parameterName) || readonlyStrategyFieldKeys.has(parameterName) || parameterName === 'id' || parameterName === 'dll') {
            runtime.lastMessage = `Parameter ${parameterName} is reserved and cannot be added as a custom strategy setting.`;
            render();
            return;
        }

        const existed = Object.prototype.hasOwnProperty.call(strategy, parameterName);
        setLocalStrategyFieldEdit(strategy.id, parameterName, parameterValue);
        clearStrategyParameterDraft(accountId, strategy.id);
        runtime.lastMessage = existed
            ? `Updated parameter ${parameterName} on strategy ${strategy.id}.`
            : `Added parameter ${parameterName} to strategy ${strategy.id}.`;
        render();
    });

    parameterRow.append(nameInput, valueInput, addButton);
    parameterField.append(parameterLabel, parameterRow);

    const parameterHint = document.createElement('p');
    parameterHint.className = 'strategy-detail-note';
    parameterHint.textContent = 'Use this for DLL-specific settings such as commission, matching_mode, or any other custom INI key.';
    parameterField.append(parameterHint);

    grid.append(parameterField);
    content.append(grid);

    const saveKey = getStrategyPresetSaveKey(accountId, strategy.id);
    const saveError = uiState.strategyPresetSaveErrors.get(saveKey) || '';
    const savedThisPreset = !saveError && (uiState.savedStrategyPresetKeys.has(saveKey)
        || uiState.lastSavedStrategyPresetKey === saveKey
        || hasSavedStrategyPresetForStrategy(strategy.id));
    const hasUnsavedChanges = !savedThisPreset && hasUnsavedStrategyPresetChanges(strategy.id);
    const isSaving = uiState.pendingStrategyPresetSaveKey === saveKey;
    const isSaved = savedThisPreset || (!saveError && !hasUnsavedChanges && (uiState.savedStrategyPresetKeys.has(saveKey)
        || uiState.lastSavedStrategyPresetKey === saveKey
        || hasSavedStrategyPresetForStrategy(strategy.id)));

    const saveRow = document.createElement('div');
    saveRow.className = 'strategy-detail-save-row';

    const saveStatus = document.createElement('span');
    saveStatus.className = `strategy-detail-save-status${hasUnsavedChanges ? ' strategy-detail-save-status--dirty' : ''}${isSaved ? ' strategy-detail-save-status--saved' : ''}${saveError ? ' strategy-detail-save-status--error' : ''}`;
    saveStatus.textContent = isSaving
        ? 'Saving preset...'
        : (saveError
            ? `Save failed: ${saveError}`
            : (hasUnsavedChanges
        ? 'Unsaved preset changes'
        : (isSaved ? 'Saved to workspace config' : 'No pending preset changes')));

    const saveButton = document.createElement('button');
    saveButton.type = 'button';
    saveButton.className = isSaved ? 'secondary-button' : 'primary-button';
    saveButton.textContent = isSaving ? 'Saving...' : (isSaved ? 'Saved' : 'Save preset');
    saveButton.disabled = isSaving || Boolean(uiState.pendingStrategyPresetSaveKey);
    saveButton.addEventListener('click', () => {
        saveStrategyPreset(strategy.id, accountId);
    });

    saveRow.append(saveStatus, saveButton);
    content.append(saveRow);
    panel.append(content);
    return panel;
}

function createStrategyRow(strategy, accountId, strategyIndex) {
    const row = document.createElement('div');
    const expanded = isAccountStrategyExpanded(accountId, strategy.id);
    const runKey = getAccountStrategyKey(accountId, strategy.id);
    const isPendingBacktestRun = uiState.pendingBacktestRunKey === runKey;
    const runtimeDetails = strategy.runtime_details?.[accountId] ?? {};
    const effectiveRuntimeStatus = getEffectiveStrategyRuntimeStatus(strategy, accountId, { isPendingBacktestRun });
    const isPendingLiveControl = state.mode === 'live' && runtime.pendingLiveControlKey === runKey;
    const liveRuntimeForThisStrategy = state.mode === 'live'
        ? findLiveRuntimeInstanceForStrategy(strategy, accountId, { statuses: ['running'] })
        : null;
    const isLiveRuntimeRunningThisStrategy = Boolean(liveRuntimeForThisStrategy);
    const isPendingLiveStop = state.mode === 'live'
        && Boolean(liveRuntimeForThisStrategy?.stop_requested)
        && isLiveRuntimeRunningThisStrategy;
    const liveRunBlocked = state.mode === 'live' && (isPendingLiveControl || isLiveRuntimeRunningThisStrategy);
    row.className = `strategy-row${expanded ? ' strategy-row--expanded' : ''}`;
    row.dataset.accountStrategyKey = runKey;
    row.dataset.accountId = accountId;
    row.dataset.strategyId = strategy.id;

    const surface = document.createElement('div');
    surface.className = 'strategy-row__surface';

    const main = document.createElement('div');
    main.className = 'strategy-row__main';

    const title = document.createElement('div');
    title.className = 'strategy-row__title';
    title.textContent = strategy.id;

    const meta = document.createElement('div');
    meta.className = 'strategy-row__meta';
    meta.textContent = `${strategy.instruments || 'No instruments'} · ${strategy.dll || 'No DLL selected'}`;

    const status = document.createElement('span');
    status.className = `strategy-status strategy-status--${effectiveRuntimeStatus}`;
    status.textContent = effectiveRuntimeStatus === 'running'
        ? 'Running'
        : (effectiveRuntimeStatus === 'failed' ? 'Failed' : 'Stopped');

    const runtimeError = String(strategy.__runtimeError ?? '').trim();

    const compactMeta = document.createElement('div');
    compactMeta.className = 'strategy-row__compact-meta';

    const quantity = document.createElement('span');
    quantity.className = 'strategy-pill';
    quantity.textContent = `Qty ${strategy.quantity || '-'}`;

    const binding = document.createElement('span');
    binding.className = 'strategy-pill strategy-pill--muted';
    binding.textContent = `Bound to ${accountId}`;

    compactMeta.append(status, quantity, binding);
    if (state.mode === 'live' && isLiveRuntimeRunningThisStrategy) {
        const connectionDots = document.createElement('div');
        connectionDots.className = 'strategy-connection-dots';

        const dotDescriptors = [
            {
                connected: runtimeDetails.market_data_connected,
                title: runtimeDetails.connection_status_known
                    ? (runtimeDetails.market_data_connected ? 'MD connected' : 'MD disconnected')
                    : 'MD status unknown'
            },
            {
                connected: runtimeDetails.trader_connected,
                title: runtimeDetails.connection_status_known
                    ? (runtimeDetails.trader_connected ? 'Trade connected' : 'Trade disconnected')
                    : 'Trade status unknown'
            }
        ];

        dotDescriptors.forEach((descriptor) => {
            const dot = document.createElement('span');
            dot.className = `strategy-connection-dot ${runtimeDetails.connection_status_known
                ? (descriptor.connected ? 'strategy-connection-dot--connected' : 'strategy-connection-dot--disconnected')
                : 'strategy-connection-dot--unknown'}`;
            dot.title = descriptor.title;
            dot.setAttribute('aria-label', descriptor.title);
            connectionDots.append(dot);
        });

        compactMeta.append(connectionDots);
    }
    main.append(title, meta, compactMeta);

    if (runtimeError) {
        const errorNote = document.createElement('div');
        errorNote.className = 'strategy-row__error';
        errorNote.textContent = runtimeError;
        main.append(errorNote);
    }

    const actions = document.createElement('div');
    actions.className = 'strategy-row__actions';

    const toggleButton = document.createElement('button');
    toggleButton.type = 'button';
    toggleButton.className = 'ghost-button';
    toggleButton.textContent = expanded ? 'Fold' : 'Unfold';
    toggleButton.setAttribute('aria-expanded', expanded ? 'true' : 'false');
    toggleButton.addEventListener('click', () => {
        toggleAccountStrategyExpanded(accountId, strategy.id);
        render();
    });

    const stopButton = document.createElement('button');
    stopButton.type = 'button';
    stopButton.className = 'secondary-button';
    stopButton.textContent = isPendingLiveStop ? 'Stopping...' : 'Stop';
    stopButton.disabled = state.mode === 'live'
        ? (isPendingLiveControl || !isLiveRuntimeRunningThisStrategy || isPendingLiveStop)
        : (isPendingBacktestRun || strategy.__runtimeStatus !== 'running');
    stopButton.addEventListener('click', () => {
        if (state.mode === 'live') {
            stopLiveRuntime(strategy.id, accountId);
            return;
        }

        strategy.__runtimeStatus = 'stopped';
        strategy.__runtimeError = '';
        runtime.lastMessage = `Marked strategy ${strategy.id} as stopped in the dashboard.`;
        render();
    });

    const runButton = document.createElement('button');
    runButton.type = 'button';
    runButton.className = `primary-button${isLiveRuntimeRunningThisStrategy ? ' primary-button--running' : ''}`;
    runButton.textContent = state.mode === 'live'
        ? (isPendingLiveControl
            ? 'Starting...'
            : (isLiveRuntimeRunningThisStrategy
                ? 'Running'
                : 'Run'))
        : (isPendingBacktestRun && normalizeBacktestDetailLevel(runtime.pendingBacktestDetailLevel) === 'full'
            ? 'Running...'
            : 'Run');
    runButton.disabled = state.mode === 'live' ? liveRunBlocked : isPendingBacktestRun;
    runButton.addEventListener('click', () => {
        if (state.mode === 'live') {
            startLiveRuntime(strategy.id, accountId);
            return;
        }

        runStrategyBacktest(strategy.id, accountId, { detailLevel: 'full' });
    });

    const runSummaryButton = document.createElement('button');
    runSummaryButton.type = 'button';
    runSummaryButton.className = 'secondary-button';
    runSummaryButton.textContent = isPendingBacktestRun && normalizeBacktestDetailLevel(runtime.pendingBacktestDetailLevel) === 'summary'
        ? 'Running summary...'
        : 'Run summary';
    runSummaryButton.disabled = isPendingBacktestRun;
    runSummaryButton.addEventListener('click', () => {
        runStrategyBacktest(strategy.id, accountId, { detailLevel: 'summary' });
    });

    const deleteButton = document.createElement('button');
    deleteButton.type = 'button';
    deleteButton.className = 'ghost-button ghost-button--danger';
    const remainingAccounts = strategy.accounts.filter((value) => value !== accountId);
    const deleteBlockedWhileRunning = effectiveRuntimeStatus === 'running';
    deleteButton.textContent = remainingAccounts.length === 0 ? 'Delete preset' : 'Remove from account';
    deleteButton.disabled = deleteBlockedWhileRunning;
    if (deleteBlockedWhileRunning) {
        deleteButton.title = 'Stop this strategy before deleting or removing its preset.';
    }
    deleteButton.addEventListener('click', () => {
        if (deleteBlockedWhileRunning) {
            runtime.lastMessage = `Stop strategy ${strategy.id} on account ${accountId} before deleting or removing its preset.`;
            render();
            return;
        }
        const remainingAccounts = strategy.accounts.filter((value) => value !== accountId);
        const willDeletePreset = remainingAccounts.length === 0;
        if (!confirmStrategyPresetRemoval(strategy, accountId, willDeletePreset)) {
            runtime.lastMessage = willDeletePreset
                ? `Cancelled deleting preset ${strategy.id}.`
                : `Cancelled removing preset ${strategy.id} from account ${accountId}.`;
            render();
            return;
        }

        clearAccountStrategyUiState(strategy.id, accountId);
        normalizeStrategyAccounts(strategy);
        if (remainingAccounts.length === 0) {
            removeStrategyById(strategy.id);
            runtime.lastMessage = `Removed strategy ${strategy.id} from account ${accountId}. The configured strategy instance was deleted because it is no longer attached to any account.`;
            render();
            return;
        }

        strategy.accounts = remainingAccounts;
        setLocalStrategiesDraft();
        runtime.lastMessage = `Detached strategy ${strategy.id} from account ${accountId}. Remaining accounts: ${strategyAccountsText(strategy)}.`;
        render();
    });

    actions.append(toggleButton, stopButton, runButton);
    if (state.mode !== 'live') {
        actions.append(runSummaryButton);
    }
    actions.append(deleteButton);
    surface.append(main, actions);
    row.append(surface);

    if (expanded) {
        const detailPanel = document.createElement('section');
        detailPanel.className = 'strategy-detail-panel';
        detailPanel.append(createAccountStrategyConfigPanel(strategy, accountId, strategyIndex));

        const tabs = [
            { id: 'positions', label: 'Positions' },
            { id: 'trades', label: 'Trades' },
            { id: 'opened-orders', label: 'Opened orders' },
            { id: 'closed-orders', label: 'Closed orders' }
        ];
        const activeTab = getActiveAccountStrategyTab(accountId, strategy.id);

        const tabList = document.createElement('div');
        tabList.className = 'strategy-detail-tabs';
        tabList.setAttribute('role', 'tablist');
        tabList.setAttribute('aria-label', `${strategy.id} account strategy details`);

        tabs.forEach((tab, index) => {
            const button = document.createElement('button');
            const isActive = activeTab === tab.id;
            const tabButtonId = `${getAccountStrategyKey(accountId, strategy.id)}-${tab.id}-tab`;
            const panelId = `${getAccountStrategyKey(accountId, strategy.id)}-${tab.id}-panel`;

            button.type = 'button';
            button.className = `strategy-detail-tab${isActive ? ' strategy-detail-tab--active' : ''}`;
            button.textContent = tab.label;
            button.id = tabButtonId;
            button.setAttribute('role', 'tab');
            button.setAttribute('aria-selected', isActive ? 'true' : 'false');
            button.setAttribute('aria-controls', panelId);
            button.tabIndex = isActive ? 0 : -1;
            button.addEventListener('click', () => {
                setActiveAccountStrategyTab(accountId, strategy.id, tab.id);
                render();
            });
            button.addEventListener('keydown', (event) => {
                if (!['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(event.key)) {
                    return;
                }

                event.preventDefault();
                let nextIndex = index;
                if (event.key === 'ArrowRight') {
                    nextIndex = (index + 1) % tabs.length;
                } else if (event.key === 'ArrowLeft') {
                    nextIndex = (index - 1 + tabs.length) % tabs.length;
                } else if (event.key === 'Home') {
                    nextIndex = 0;
                } else if (event.key === 'End') {
                    nextIndex = tabs.length - 1;
                }

                setActiveAccountStrategyTab(accountId, strategy.id, tabs[nextIndex].id);
                render();
            });

            tabList.append(button);
        });

        detailPanel.append(tabList);

        tabs.forEach((tab) => {
            const isActive = activeTab === tab.id;
            const panel = document.createElement('div');
            panel.className = 'strategy-detail-panel__body';
            panel.id = `${getAccountStrategyKey(accountId, strategy.id)}-${tab.id}-panel`;
            panel.setAttribute('role', 'tabpanel');
            panel.setAttribute('aria-labelledby', `${getAccountStrategyKey(accountId, strategy.id)}-${tab.id}-tab`);
            panel.tabIndex = 0;
            panel.hidden = !isActive;
            panel.append(createAccountStrategyDetailPanel(tab.id, strategy, accountId));
            detailPanel.append(panel);
        });

        row.append(detailPanel);
    }

    return row;
}

function normalizeStrategyBindings() {
    const accountIds = state.accounts.map((account) => account.id).filter(Boolean);
    state.strategies.forEach((strategy) => {
        ensureStrategyUiState(strategy);
        normalizeStrategyAccounts(strategy);
        strategy.accounts = strategy.accounts.filter((accountId) => accountIds.includes(accountId));
    });
}

function createAccountStrategyAssignmentFieldLegacy(account) {
    const wrapper = document.createElement('div');
    wrapper.className = 'field field--full strategy-manager';

    const label = document.createElement('label');
    label.textContent = 'Account Strategies';

    const toolbar = document.createElement('div');
    toolbar.className = 'strategy-manager__toolbar';

    const addButton = document.createElement('button');
    addButton.type = 'button';
    addButton.className = 'secondary-button';
    addButton.textContent = 'Add strategy';

    const picker = document.createElement('div');
    picker.className = 'strategy-picker-panel hidden';

    const availableStrategies = getAvailableStrategiesForAccount(account.id);
    if (availableStrategies.length === 0) {
        const empty = document.createElement('div');
        empty.className = 'assignment-empty';
        empty.textContent = 'No other strategies are available right now.';
        picker.append(empty);
    } else {
        availableStrategies.forEach((strategy) => {
            const item = document.createElement('div');
            item.className = 'strategy-picker-item';

            const info = document.createElement('div');
            info.className = 'strategy-picker-item__info';

            const name = document.createElement('div');
            name.className = 'strategy-picker-item__title';
            name.textContent = strategy.id;

            const meta = document.createElement('div');
            meta.className = 'strategy-picker-item__meta';
            meta.textContent = `${strategyAccountsText(strategy)} · ${strategy.instruments || 'No instruments'}`;

            info.append(name, meta);

            const action = document.createElement('button');
            action.type = 'button';
            action.className = 'primary-button';
            action.textContent = 'Attach here';
            action.addEventListener('click', () => {
                normalizeStrategyAccounts(strategy);
                strategy.accounts = [...strategy.accounts, account.id];
                normalizeStrategyAccounts(strategy);
                ensureStrategyUiState(strategy);
                setLocalStrategiesDraft();
                runtime.lastMessage = `Attached strategy ${strategy.id} to account ${account.id}. Current accounts: ${strategyAccountsText(strategy)}.`;
                render();
            });

            item.append(info, action);
            picker.append(item);
        });
    }

    addButton.addEventListener('click', () => {
        picker.classList.toggle('hidden');
    });

    toolbar.append(addButton, picker);

    const assignedRows = document.createElement('div');
    assignedRows.className = 'strategy-rows';

    assignedRows.append(createAccountSummarySection(account));
    const liveInventorySection = createLiveInventoryManagerSection(account);
    if (liveInventorySection) {
        assignedRows.append(liveInventorySection);
    }

    const assigned = getAssignedStrategies(account.id);
    if (assigned.length === 0) {
        const empty = document.createElement('div');
        empty.className = 'assignment-empty';
        empty.textContent = 'No strategies attached to this account yet.';
        assignedRows.append(empty);
    } else {
        assigned.forEach((strategy) => {
            assignedRows.append(createStrategyRow(strategy, account.id, state.strategies.indexOf(strategy)));
        });
    }

    const hint = document.createElement('p');
    hint.className = 'assignment-hint';
    hint.textContent = 'Choose any strategy from the strategy catalog here. A single strategy can be attached to multiple accounts at the same time. Run and stop buttons update the dashboard control state for each attached strategy.';

    wrapper.append(label, toolbar, assignedRows, hint);
    return wrapper;
}

function createAccountStrategyAssignmentField(account) {
    const wrapper = document.createElement('div');
    wrapper.className = 'field field--full strategy-manager';

    const label = document.createElement('label');
    label.textContent = 'Account Strategies';

    const toolbar = document.createElement('div');
    toolbar.className = 'strategy-manager__toolbar';

    const addButton = document.createElement('button');
    addButton.type = 'button';
    addButton.className = 'secondary-button';
    addButton.disabled = uiState.pendingStrategyBrowseAccount !== null;
    addButton.textContent = uiState.pendingStrategyBrowseAccount === account.id ? 'Loading...' : 'Add strategy';
    addButton.addEventListener('click', () => {
        browseStrategyDll(account.id);
    });

    toolbar.append(addButton);

    const assignedRows = document.createElement('div');
    assignedRows.className = 'strategy-rows';

    assignedRows.append(createAccountSummarySection(account));
    const liveInventorySection = createLiveInventoryManagerSection(account);
    if (liveInventorySection) {
        assignedRows.append(liveInventorySection);
    }

    const assigned = getAssignedStrategies(account.id);
    if (assigned.length === 0) {
        const empty = document.createElement('div');
        empty.className = 'assignment-empty';
        empty.textContent = 'No strategies attached to this account yet.';
        assignedRows.append(empty);
    } else {
        assigned.forEach((strategy) => {
            assignedRows.append(createStrategyRow(strategy, account.id, state.strategies.indexOf(strategy)));
        });
    }

    const hint = document.createElement('p');
    hint.className = 'assignment-hint';
    hint.textContent = state.mode === 'live'
        ? 'Click Add strategy to choose from the automatic strategy catalog or type a remote path manually. In live mode, Run saves the current config and launches an independent dashboard-scoped itrader.exe for the selected strategy; Stop halts that strategy scope without stopping other running strategies.'
        : 'Click Add strategy to choose from the automatic strategy catalog or type a remote path manually. Configure that strategy’s parameters directly inside this account panel after it is added.';

    wrapper.append(label, toolbar, assignedRows, hint);
    return wrapper;
}

function updateAccountField(account, key, nextValue) {
    const previousAccountId = account.id;
    account[key] = nextValue;
    if (key === 'id') {
        moveAccountSummaryTab(previousAccountId, nextValue);
        state.strategies.forEach((strategy) => {
            normalizeStrategyAccounts(strategy);
            strategy.accounts = strategy.accounts.map((accountId) => accountId === previousAccountId ? nextValue : accountId);
        });
        setLocalStrategiesDraft();
    }
}

function openAccountConfig(account) {
    const accountId = typeof account === 'string' ? account : account?.id;
    uiState.activeAccountConfig = state.accounts.find((candidate) => candidate.id === accountId) || account || null;
    render();
}

function closeAccountConfig() {
    uiState.activeAccountConfig = null;
    render();
}

function getActiveAccountConfig() {
    if (uiState.activeAccountConfig && state.accounts.includes(uiState.activeAccountConfig)) {
        return uiState.activeAccountConfig;
    }

    const activeAccountId = uiState.activeAccountConfig?.id;
    if (activeAccountId) {
        const refreshedAccount = state.accounts.find((account) => account.id === activeAccountId);
        if (refreshedAccount) {
            uiState.activeAccountConfig = refreshedAccount;
            return refreshedAccount;
        }
    }

    uiState.activeAccountConfig = null;
    return null;
}

function renderAccountConfigModal() {
    const activeAccount = getActiveAccountConfig();
    const fields = accountFieldGroups[state.mode];
    if (!activeAccount) {
        accountConfigModal.classList.add('hidden');
        accountConfigModal.setAttribute('aria-hidden', 'true');
        accountConfigTitle.textContent = 'Configure account';
        accountConfigSubtitle.textContent = 'Open an account to edit its detailed fields.';
        accountConfigForm.innerHTML = '';
        return;
    }

    accountConfigModal.classList.remove('hidden');
    accountConfigModal.setAttribute('aria-hidden', 'false');
    accountConfigTitle.textContent = activeAccount.id || 'Configure account';
    accountConfigSubtitle.textContent = state.mode === 'live'
        ? 'Edit trader, market-data, and credential-related fields here. Strategy attachments stay on the main account card.'
        : 'Edit the account identifier and cash settings here. Strategy attachments stay on the main account card.';

    accountConfigForm.innerHTML = '';
    fields.forEach(([key, label]) => {
        const fullWidth = key.includes('dir') || key.includes('front') || key === 'password' || key === 'auth_code' || key === 'product_info';
        const focusKey = `account-config:${state.accounts.indexOf(activeAccount)}:${key}`;

        if (key === 'production_mode') {
            accountConfigForm.append(createSelectField(label, activeAccount[key] ?? 'true', ['true', 'false'], (nextValue) => {
                updateAccountField(activeAccount, key, nextValue);
                render();
            }, fullWidth, focusKey));
            return;
        }

        const inputType = key === 'password' || key === 'auth_code' ? 'password' : 'text';
        accountConfigForm.append(createField(label, activeAccount[key] ?? '', (nextValue) => {
            updateAccountField(activeAccount, key, nextValue);
            render();
        }, fullWidth, inputType, focusKey));
    });
}

function renderBacktestCompletionModal() {
    if (!backtestDoneModal || !backtestDoneTitle || !backtestDoneMessage || !backtestDoneMeta) {
        return;
    }

    const isVisible = Boolean(runtime.backtestCompletionVisible);
    backtestDoneModal.classList.toggle('hidden', !isVisible);
    backtestDoneModal.setAttribute('aria-hidden', isVisible ? 'false' : 'true');

    if (!isVisible) {
        backtestDoneTitle.textContent = 'Backtesting Done';
        backtestDoneMessage.textContent = 'Backtest replay completed successfully.';
        backtestDoneMeta.textContent = '';
        backtestDoneMeta.classList.add('hidden');
        return;
    }

    backtestDoneTitle.textContent = runtime.backtestCompletionTitle || 'Backtesting Done';
    backtestDoneMessage.textContent = runtime.backtestCompletionMessage || 'Backtest replay completed successfully.';
    if (runtime.backtestCompletionMeta) {
        backtestDoneMeta.textContent = runtime.backtestCompletionMeta;
        backtestDoneMeta.classList.remove('hidden');
    } else {
        backtestDoneMeta.textContent = '';
        backtestDoneMeta.classList.add('hidden');
    }
}

function renderAccounts() {
    const nextAccountsKey = buildAccountsRenderKey();
    if (renderSectionCache.accountsKey === nextAccountsKey && accountsList.childElementCount > 0) {
        return;
    }

    renderSectionCache.accountsKey = nextAccountsKey;
    accountsList.innerHTML = '';
    normalizeStrategyBindings();

    state.accounts.forEach((account, accountIndex) => {
        const node = accountTemplate.content.firstElementChild.cloneNode(true);
        node.dataset.accountId = account.id || `account_${accountIndex + 1}`;
        node.querySelector('.entity-card__title').textContent = account.id || `account_${accountIndex + 1}`;
        node.querySelector('[data-action="config-account"]').addEventListener('click', () => {
            openAccountConfig(account);
        });
        node.querySelector('[data-action="remove-account"]').addEventListener('click', () => {
            uiState.accountSummaryTabs.delete(account.id);
            uiState.expandedLiveInventoryAccounts.delete(account.id);
            if (uiState.activeAccountConfig === account) {
                uiState.activeAccountConfig = null;
            }
            state.accounts.splice(accountIndex, 1);
            if (state.accounts.length === 0) {
                state.accounts.push(defaultAccount(state.mode));
            }
            render();
        });

        const grid = node.querySelector('.account-fields');
        grid.append(createAccountStrategyAssignmentField(account));

        accountsList.append(node);
    });
}

function renderStrategies() {
    strategiesList.innerHTML = '';
    normalizeStrategyBindings();

    if (strategyCatalogButton) {
        strategyCatalogButton.textContent = runtime.strategyFileCatalogStatus === 'loading' ? 'Scanning...' : 'Rescan';
        strategyCatalogButton.disabled = runtime.strategyFileCatalogStatus === 'loading' || runtime.pendingStrategyUpload;
    }
    if (strategyUploadButton) {
        strategyUploadButton.textContent = runtime.pendingStrategyUpload ? 'Uploading...' : 'Upload';
        strategyUploadButton.disabled = runtime.pendingStrategyUpload || runtime.strategyFileCatalogStatus === 'loading';
    }

    if (runtime.strategyFileCatalogStatus === 'loading' && runtime.strategyFileCatalog.length === 0) {
        const empty = document.createElement('article');
        empty.className = 'entity-card';
        empty.innerHTML = `
            <div class="entity-card__header">
                <div>
                    <p class="entity-card__eyebrow">Strategies</p>
                    <h4 class="entity-card__title">Scanning strategy catalog</h4>
                </div>
            </div>
            <div class="form-grid">
                <div class="field field--full">
                    <div class="field__readonly">Scanning ${escapeHtml(runtime.strategyFileCatalogRoot)} for strategy DLLs...</div>
                </div>
            </div>
        `;
        strategiesList.append(empty);
        return;
    }

    if (runtime.strategyFileCatalog.length === 0) {
        const empty = document.createElement('article');
        empty.className = 'entity-card';
        empty.innerHTML = `
            <div class="entity-card__header">
                <div>
                    <p class="entity-card__eyebrow">Strategies</p>
                    <h4 class="entity-card__title">No strategy DLLs found</h4>
                </div>
            </div>
            <div class="form-grid">
                <div class="field field--full">
                    <div class="field__readonly">${escapeHtml(runtime.strategyFileCatalogStatus === 'error'
                        ? `Unable to scan ${runtime.strategyFileCatalogRoot}: ${runtime.strategyFileCatalogError || 'unknown error'}`
                        : `No strategy DLLs were found under ${runtime.strategyFileCatalogRoot}.`)}</div>
                </div>
                <div class="field field--full">
                    <div class="field__readonly">Add strategies from an account panel after the catalog is available. Parameters are configured in the account panel, not here.</div>
                </div>
            </div>
        `;
        strategiesList.append(empty);
        return;
    }

    runtime.strategyFileCatalog.forEach((entry, entryIndex) => {
        const card = document.createElement('article');
        card.className = 'entity-card';

        const header = document.createElement('div');
        header.className = 'entity-card__header';

        const copy = document.createElement('div');
        const eyebrow = document.createElement('p');
        eyebrow.className = 'entity-card__eyebrow';
        eyebrow.textContent = 'Catalog Strategy';

        const title = document.createElement('h4');
        title.className = 'entity-card__title';
        title.textContent = entry.id || strategyBaseName(entry.dll) || `strategy_${entryIndex + 1}`;
        copy.append(eyebrow, title);
        header.append(copy);

        const configuredStrategies = getCatalogEntryConfiguredStrategies(entry);
        const attachedStrategies = configuredStrategies.filter((strategy) => strategy.accounts.length > 0);
        const attachedAccounts = uniqueValues(attachedStrategies.flatMap((strategy) => strategy.accounts));
        const unassignedDefinitions = configuredStrategies.length - attachedStrategies.length;
        const usageText = configuredStrategies.length === 0
            ? 'Not added to any account yet. Use Add strategy from an account panel to create and configure a saved strategy definition there.'
            : `${configuredStrategies.length} saved strategy definition${configuredStrategies.length === 1 ? '' : 's'} currently ${[
                attachedStrategies.length > 0 ? `${attachedStrategies.length} attached to ${attachedAccounts.join(', ')}` : '',
                unassignedDefinitions > 0 ? `${unassignedDefinitions} unassigned` : ''
            ].filter(Boolean).join(' and ')}.`;

        const grid = document.createElement('div');
        grid.className = 'form-grid strategy-fields';
        grid.append(
            createReadonlyField('Remote Path', entry.dll || entry.absolute_path || 'Unknown remote path', true),
            createReadonlyField('Usage', usageText, true)
        );
        if (entry.absolute_path && entry.absolute_path !== entry.dll) {
            grid.append(createReadonlyField('Local Path', entry.absolute_path, true));
        }

        const note = document.createElement('p');
        note.className = 'strategy-detail-note';
        note.textContent = 'This panel lists the available strategy DLLs automatically. Configure parameters after adding a strategy from an account panel.';

        card.append(header, grid, note);
        strategiesList.append(card);
    });
}

function renderBacktestSourceSettings() {
    if (!backtestSourceSection || !backtestSourceForm) {
        return;
    }

    const isBacktest = state.mode === 'backtest';
    backtestSourceSection.classList.toggle('hidden', !isBacktest);
    backtestSourceForm.innerHTML = '';

    if (!isBacktest) {
        return;
    }

    backtestSourceForm.append(
        createActionField('Backtest Data Dir', state.backtest.data_dir ?? '', (nextValue) => {
            state.backtest.data_dir = nextValue;
            render();
        }, runtime.pendingBacktestDirectoryBrowse ? 'Opening...' : 'Browse...', () => {
            browseBacktestDirectory();
        }, {
            fullWidth: true,
            actionDisabled: runtime.pendingBacktestDirectoryBrowse,
            focusKey: 'backtest-source:data_dir'
        }),
        createField('AGTICK File (optional)', state.backtest.csv ?? '', (nextValue) => {
            state.backtest.csv = nextValue;
            render();
        }, true, 'text', 'backtest-source:csv'),
        createReadonlyField(
            'Load Priority',
            'Each strategy can specify its own backtest data dir below. If a strategy leaves that field empty, the runtime falls back to this default directory first, and then to the shared AGTICK file. On remote UI/API deployments, Browse may be unavailable; type the server-side path manually.',
            true
        )
    );
}

function renderRecorderSettings() {
    if (!recorderConfigSection || !recorderConfigForm || !recorderConfigPreview) {
        return;
    }

    const isLive = state.mode === 'live';
    recorderConfigSection.classList.toggle('hidden', !isLive);
    if (!isLive) {
        recorderConfigForm.innerHTML = '';
        recorderConfigPreview.textContent = '';
        recorderConfigPreview.classList.remove('hidden');
        return;
    }

    const recorder = getRecorderState();
    const recorderRuntime = getRecorderRuntimeState();
    const recorderPanelCollapsed = Boolean(uiState.recorderPanelCollapsed);
    if (toggleRecorderPanelButton) {
        toggleRecorderPanelButton.textContent = recorderPanelCollapsed ? 'Unfold' : 'Fold';
        toggleRecorderPanelButton.setAttribute('aria-expanded', recorderPanelCollapsed ? 'false' : 'true');
    }
    if (saveRecorderConfigButton) {
        saveRecorderConfigButton.disabled = runtime.pendingRecorderControlAction;
        saveRecorderConfigButton.textContent = uiState.localRecorderDraft === null ? 'Save recorder INI' : 'Save recorder INI *';
    }
    if (startRecorderButton) {
        startRecorderButton.disabled = runtime.pendingRecorderControlAction || recorderRuntime.running;
        startRecorderButton.textContent = runtime.pendingRecorderControlAction && !recorderRuntime.running ? 'Starting...' : 'Start recorder';
    }
    if (stopRecorderButton) {
        stopRecorderButton.disabled = runtime.pendingRecorderControlAction || !recorderRuntime.running;
        stopRecorderButton.textContent = runtime.pendingRecorderControlAction && recorderRuntime.running ? 'Stopping...' : 'Stop recorder';
    }

    recorderConfigForm.innerHTML = '';
    if (recorderPanelCollapsed) {
        const summaryWrapper = document.createElement('div');
        summaryWrapper.className = 'field field--full';

        const summary = document.createElement('div');
        summary.className = 'strategy-summary';
        summary.append(
            createSummaryCard('Recorder Status', recorderRuntime.status || 'stopped'),
            createSummaryCard('Recorder PID', recorderRuntime.process_id > 0 ? String(recorderRuntime.process_id) : 'Not running'),
            createSummaryCard('Auto Restart', recorderRuntime.auto_restart_enabled ? `On (${recorderRuntime.auto_restart_count})` : 'Off'),
            createSummaryCard('Managed By', formatRecorderManager(recorderRuntime)),
            createSummaryCard('Config Path', recorderRuntime.config_path || recorder.config_path || 'configs/ctp_md_recorder.ini'),
            createSummaryCard('MD Front', recorder.md_front || recorder.front || 'Not configured')
        );
        summaryWrapper.append(summary);

        const noteWrapper = document.createElement('div');
        noteWrapper.className = 'field field--full';

        const note = document.createElement('p');
        note.className = 'strategy-detail-note';
        note.textContent = recorderRuntime.message || 'Recorder controls stay available in the header. Click Unfold to edit credentials or inspect the generated INI.';
        noteWrapper.append(note);

        recorderConfigForm.append(summaryWrapper, noteWrapper);
        recorderConfigPreview.textContent = '';
        recorderConfigPreview.classList.add('hidden');
        return;
    }

    recorderConfigPreview.classList.remove('hidden');
    recorderConfigForm.append(
        createReadonlyField('Recorder Status', recorderRuntime.status || 'stopped'),
        createReadonlyField('Recorder PID', recorderRuntime.process_id > 0 ? String(recorderRuntime.process_id) : 'Not running'),
        createReadonlyField('Auto Restart', recorderRuntime.auto_restart_enabled ? `On (${recorderRuntime.auto_restart_count})` : 'Off'),
        createReadonlyField('Managed By', formatRecorderManager(recorderRuntime)),
        createReadonlyField('Recorder Message', recorderRuntime.message || 'Recorder is not running yet.', true)
    );

    recorderFieldGroups.forEach((field) => {
        const focusKey = `recorder:${field.key}`;
        if (field.readonly) {
            recorderConfigForm.append(createReadonlyField(field.label, recorder[field.key] ?? '', field.fullWidth));
            return;
        }

        if (Array.isArray(field.options)) {
            recorderConfigForm.append(createSelectField(field.label, recorder[field.key] ?? field.options[0], field.options, (nextValue) => {
                setLocalRecorderField(field.key, nextValue);
                refreshRecorderConfigPreview();
            }, field.fullWidth, focusKey));
            return;
        }

        recorderConfigForm.append(createField(field.label, recorder[field.key] ?? '', (nextValue) => {
            setLocalRecorderField(field.key, nextValue);
            refreshRecorderConfigPreview();
        }, field.fullWidth, field.inputType ?? 'text', focusKey));
    });

    recorderConfigPreview.textContent = generateRecorderIni();
}

function refreshRecorderConfigPreview() {
    if (saveRecorderConfigButton) {
        saveRecorderConfigButton.textContent = uiState.localRecorderDraft === null ? 'Save recorder INI' : 'Save recorder INI *';
    }
    if (recorderConfigPreview && !recorderConfigPreview.classList.contains('hidden')) {
        recorderConfigPreview.textContent = generateRecorderIni();
    }
}

function renderLiveExecutionSettings() {
    if (!liveExecutionSection || !liveExecutionForm) {
        return;
    }

    const isLive = state.mode === 'live';
    liveExecutionSection.classList.toggle('hidden', !isLive);
    liveExecutionForm.innerHTML = '';

    if (!isLive) {
        return;
    }

    const dryRunEnabled = isLiveDryRunEnabled();
    const liveRuntime = getLiveRuntimeState();
    const appliesOnNextLaunch = isLiveRuntimeActiveForCurrentConfig();

    const wrapper = document.createElement('div');
    wrapper.className = 'field field--full live-guard';

    const card = document.createElement('div');
    card.className = 'live-guard__card';

    const copy = document.createElement('div');
    copy.className = 'live-guard__copy';

    const title = document.createElement('div');
    title.className = 'live-guard__title';
    title.textContent = 'Platform-level dry run';

    const status = document.createElement('span');
    status.className = `live-guard__status ${dryRunEnabled ? 'live-guard__status--enabled' : 'live-guard__status--disabled'}`;
    status.textContent = dryRunEnabled ? 'Broker blocked, paper fills enabled' : 'Broker routing enabled';

    const description = document.createElement('p');
    description.className = 'live-guard__description';
    description.textContent = dryRunEnabled
        ? 'Trader / MD connections, warmup, broker recovery, and strategy logic still run normally. Broker submissions are skipped before ReqOrderInsert, while local paper fills keep entry/exit logic and chart signals active.'
        : 'When disabled, orders that pass the live risk gate can continue to the configured broker gateway.';

    const hint = document.createElement('p');
    hint.className = 'live-guard__hint';
    hint.textContent = appliesOnNextLaunch
        ? `The current live runtime is already running for ${liveRuntime.active_config_path || runtime.sourceConfig}. Save and relaunch to apply a changed dry-run setting.`
        : 'Save the generated INI and launch the live runtime to apply this setting.';

    copy.append(title, status, description, hint);

    const toggleButton = document.createElement('button');
    toggleButton.type = 'button';
    toggleButton.className = `live-toggle${dryRunEnabled ? ' live-toggle--enabled' : ''}`;
    toggleButton.setAttribute('role', 'switch');
    toggleButton.setAttribute('aria-checked', dryRunEnabled ? 'true' : 'false');
    toggleButton.setAttribute('aria-pressed', dryRunEnabled ? 'true' : 'false');
    toggleButton.innerHTML = `
        <span class="live-toggle__track" aria-hidden="true">
            <span class="live-toggle__knob"></span>
        </span>
        <span class="live-toggle__label">${dryRunEnabled ? 'Dry run ON' : 'Dry run OFF'}</span>
    `;
    toggleButton.addEventListener('click', () => {
        const nextValue = !dryRunEnabled;
        setLiveDryRunEnabled(nextValue);
        runtime.lastMessage = nextValue
            ? 'Enabled dry run in the live config draft. Save and relaunch to block broker submissions while recording local paper fills.'
            : 'Disabled dry run in the live config draft. Save and relaunch to restore broker order routing.';
        render();
    });

    card.append(copy, toggleButton);
    wrapper.append(card);
    liveExecutionForm.append(wrapper);
}

function escapeIniValue(value) {
    return String(value ?? '')
        .replaceAll('\r', ' ')
        .replaceAll('\n', ' ')
        .trim();
}

function validateLiveInventoryAdjustments() {
    const inventory = getLiveInventoryState();
    const seenIds = new Set();
    const deltaFields = [
        ['long_today_delta', 'long_today_average_price'],
        ['long_yesterday_delta', 'long_yesterday_average_price'],
        ['short_today_delta', 'short_today_average_price'],
        ['short_yesterday_delta', 'short_yesterday_average_price']
    ];
    const priceFields = [
        'long_today_average_price',
        'long_yesterday_average_price',
        'short_today_average_price',
        'short_yesterday_average_price'
    ];

    inventory.adjustments.forEach((adjustment, index) => {
        const label = adjustment.id || `adjustment #${index + 1}`;
        if (!adjustment.id) {
            throw new Error(`Live inventory adjustment #${index + 1} is missing an adjustment id.`);
        }
        if (seenIds.has(adjustment.id)) {
            throw new Error(`Live inventory adjustment id ${adjustment.id} is duplicated.`);
        }
        seenIds.add(adjustment.id);

        ['account_id', 'strategy_id', 'instrument', 'reason_code'].forEach((key) => {
            if (!String(adjustment[key] ?? '').trim()) {
                throw new Error(`Live inventory adjustment ${label} is missing ${key}.`);
            }
        });

        deltaFields.forEach(([field, averagePriceField]) => {
            const raw = String(adjustment[field] ?? '0').trim() || '0';
            if (!/^-?\d+$/.test(raw)) {
                throw new Error(`Live inventory adjustment ${label} has an invalid integer for ${field}.`);
            }

            if (Number(raw) > 0 && !String(adjustment[averagePriceField] ?? '').trim()) {
                throw new Error(`Live inventory adjustment ${label} increases ${field} but does not provide ${averagePriceField}.`);
            }
        });

        priceFields.forEach((field) => {
            const raw = String(adjustment[field] ?? '').trim();
            if (!raw) {
                return;
            }
            if (!Number.isFinite(Number(raw))) {
                throw new Error(`Live inventory adjustment ${label} has an invalid numeric value for ${field}.`);
            }
        });
    });
}

function generateLiveInventoryAdjustmentsIni() {
    const inventory = getLiveInventoryState();
    const adjustments = [...inventory.adjustments].sort((left, right) => {
        const leftKey = `${left.account_id}|${left.id}`;
        const rightKey = `${right.account_id}|${right.id}`;
        return leftKey.localeCompare(rightKey);
    });

    const lines = [
        '; Generated by iTrader dashboard',
        '; Each inventory_adjustment.<id> is applied once by the live runtime.',
        '; To apply a new change after an adjustment has already been used, clone it with a new id.',
        ''
    ];

    adjustments.forEach((adjustment) => {
        lines.push(`[inventory_adjustment.${escapeIniValue(adjustment.id)}]`);
        lines.push(`enabled=${adjustment.enabled ? 'true' : 'false'}`);
        lines.push(`account_id=${escapeIniValue(adjustment.account_id)}`);
        lines.push(`strategy_id=${escapeIniValue(adjustment.strategy_id)}`);
        lines.push(`instrument=${escapeIniValue(adjustment.instrument)}`);
        if (adjustment.exchange) {
            lines.push(`exchange=${escapeIniValue(adjustment.exchange)}`);
        }
        lines.push(`operator_id=${escapeIniValue(adjustment.operator_id || 'dashboard')}`);
        lines.push(`reason_code=${escapeIniValue(adjustment.reason_code)}`);
        if (adjustment.reason_text) {
            lines.push(`reason_text=${escapeIniValue(adjustment.reason_text)}`);
        }
        lines.push(`long_today_delta=${escapeIniValue(adjustment.long_today_delta || '0')}`);
        if (adjustment.long_today_average_price) {
            lines.push(`long_today_average_price=${escapeIniValue(adjustment.long_today_average_price)}`);
        }
        lines.push(`long_yesterday_delta=${escapeIniValue(adjustment.long_yesterday_delta || '0')}`);
        if (adjustment.long_yesterday_average_price) {
            lines.push(`long_yesterday_average_price=${escapeIniValue(adjustment.long_yesterday_average_price)}`);
        }
        lines.push(`short_today_delta=${escapeIniValue(adjustment.short_today_delta || '0')}`);
        if (adjustment.short_today_average_price) {
            lines.push(`short_today_average_price=${escapeIniValue(adjustment.short_today_average_price)}`);
        }
        lines.push(`short_yesterday_delta=${escapeIniValue(adjustment.short_yesterday_delta || '0')}`);
        if (adjustment.short_yesterday_average_price) {
            lines.push(`short_yesterday_average_price=${escapeIniValue(adjustment.short_yesterday_average_price)}`);
        }
        lines.push('');
    });

    return `${lines.join('\n').trim()}\n`;
}

function validatePersistedInventoryStoreEdits() {
    const inventory = getLiveInventoryState();
    const integerFields = [
        'long_today_quantity',
        'long_yesterday_quantity',
        'short_today_quantity',
        'short_yesterday_quantity'
    ];
    const priceFields = [
        'long_average_price',
        'short_average_price'
    ];

    inventory.persisted_positions.forEach((position, index) => {
        const label = `${position.strategy_id || 'strategy'} ${position.instrument || `row ${index + 1}`}`;
        ['store_path', 'account_id', 'strategy_id', 'instrument'].forEach((field) => {
            if (!String(position[field] ?? '').trim()) {
                throw new Error(`Persisted inventory ${label} is missing ${field}.`);
            }
        });

        integerFields.forEach((field) => {
            const raw = String(position[field] ?? '0').trim() || '0';
            if (!/^\d+$/.test(raw)) {
                throw new Error(`Persisted inventory ${label} has an invalid non-negative integer for ${field}.`);
            }
        });

        priceFields.forEach((field) => {
            const raw = String(position[field] ?? '').trim();
            if (!raw) {
                return;
            }
            if (!Number.isFinite(Number(raw))) {
                throw new Error(`Persisted inventory ${label} has an invalid numeric value for ${field}.`);
            }
        });
    });
}

function generateLiveInventoryStoreEditsIni() {
    const inventory = getLiveInventoryState();
    const positions = cloneLiveInventoryPersistedPositions(inventory.persisted_positions);
    const lines = [
        '; Generated by iTrader dashboard',
        '; Direct persisted per-strategy inventory edits. These update strategy_inventory_store.ini.',
        ''
    ];

    positions.forEach((position, index) => {
        recomputePersistedInventoryPosition(position);
        lines.push(`[persisted_inventory_position.${index + 1}]`);
        lines.push(`store_path=${escapeIniValue(position.store_path)}`);
        lines.push(`account_id=${escapeIniValue(position.account_id)}`);
        lines.push(`strategy_id=${escapeIniValue(position.strategy_id)}`);
        lines.push(`instrument=${escapeIniValue(position.instrument)}`);
        lines.push(`long_today_quantity=${escapeIniValue(position.long_today_quantity || '0')}`);
        lines.push(`long_yesterday_quantity=${escapeIniValue(position.long_yesterday_quantity || '0')}`);
        lines.push(`long_average_price=${escapeIniValue(position.long_average_price || '0')}`);
        lines.push(`short_today_quantity=${escapeIniValue(position.short_today_quantity || '0')}`);
        lines.push(`short_yesterday_quantity=${escapeIniValue(position.short_yesterday_quantity || '0')}`);
        lines.push(`short_average_price=${escapeIniValue(position.short_average_price || '0')}`);
        lines.push('');
    });

    return `${lines.join('\n').trim()}\n`;
}

function generateRecorderIni() {
    const recorder = getRecorderState();
    const lines = [];
    const appendIfPresent = (key, value) => {
        if (value === undefined || value === null) {
            return;
        }

        const trimmed = String(value).trim();
        if (!trimmed) {
            return;
        }

        lines.push(`${key}=${escapeIniValue(trimmed)}`);
    };

    lines.push('[platform]');
    lines.push('mode=ctp_md_recorder');
    lines.push('');
    lines.push('[recorder]');
    appendIfPresent('output_dir', recorder.output_dir);
    appendIfPresent('instruments', recorder.instruments);
    appendIfPresent('flush_interval_ms', recorder.flush_interval_ms);
    appendIfPresent('status_interval_ms', recorder.status_interval_ms);
    appendIfPresent('idle_sleep_ms', recorder.idle_sleep_ms);
    appendIfPresent('connect_timeout_ms', recorder.connect_timeout_ms);
    appendIfPresent('deduplicate_exact_ticks', recorder.deduplicate_exact_ticks);
    appendIfPresent('auto_restart_enabled', recorder.auto_restart_enabled);
    lines.push('');
    lines.push(`[account.${escapeIniValue(recorder.account_id || 'recorder')}]`);
    appendIfPresent('md_front', recorder.front);
    appendIfPresent('md_broker_id', recorder.broker_id);
    appendIfPresent('md_user_id', recorder.user_id);
    appendIfPresent('md_password', recorder.password);
    appendIfPresent('product_info', recorder.product_info);
    appendIfPresent('md_flow_dir', recorder.flow_dir);
    appendIfPresent('production_mode', recorder.production_mode);
    appendIfPresent('reconnect_enabled', recorder.reconnect_enabled);
    appendIfPresent('reconnect_retry_interval_ms', recorder.reconnect_retry_interval_ms);
    appendIfPresent('reconnect_max_attempts', recorder.reconnect_max_attempts);

    return `${lines.join('\n').trim()}\n`;
}

async function refreshLiveInventoryFromApi({ silent = false } = {}) {
    if (state.mode !== 'live') {
        return;
    }

    runtime.pendingLiveInventoryRefresh = true;
    if (!silent) {
        runtime.lastMessage = 'Refreshing live inventory adjustments and reconciliation status...';
        render();
    }

    try {
        const response = await fetch(`${API_BASE}/api/live-inventory-adjustments?${buildModeQuery('live').toString()}`);
        if (!response.ok) {
            throw new Error(`HTTP ${response.status}`);
        }

        const payload = await response.json();
        if (!silent) {
            clearLocalLiveInventoryDraft();
        }
        state.live_inventory = normalizeLiveInventoryPayload(payload);
        runtime.apiConnected = true;
        if (!silent) {
            runtime.lastMessage = `Refreshed live inventory adjustments from ${state.live_inventory.adjustments_path || 'runtime/strategy_inventory_adjustments.ini'}.`;
        }
    } catch (error) {
        const detail = error instanceof Error ? error.message : String(error);
        if (!silent) {
            runtime.lastMessage = `Unable to refresh live inventory adjustments: ${detail}.`;
        }
    } finally {
        runtime.pendingLiveInventoryRefresh = false;
        render();
    }
}

async function saveLiveInventoryAdjustments() {
    if (state.mode !== 'live') {
        return;
    }

    try {
        validateLiveInventoryAdjustments();
        validatePersistedInventoryStoreEdits();
    } catch (error) {
        runtime.lastMessage = error instanceof Error ? error.message : String(error);
        render();
        return;
    }

    runtime.pendingLiveInventorySave = true;
    runtime.lastMessage = uiState.localPersistedInventoryDraft === null
        ? 'Saving live inventory adjustments to the local API...'
        : 'Saving live inventory adjustments and persisted store edits to the local API...';
    render();

    try {
        const savingPersistedStoreEdits = uiState.localPersistedInventoryDraft !== null;
        let response = await fetch(`${API_BASE}/api/live-inventory-adjustments?${buildModeQuery('live').toString()}`, {
            method: 'POST',
            headers: {
                'Content-Type': 'text/plain;charset=utf-8'
            },
            body: generateLiveInventoryAdjustmentsIni()
        });

        if (!response.ok) {
            throw new Error(`HTTP ${response.status}`);
        }

        let payload = await response.json();
        if (savingPersistedStoreEdits) {
            response = await fetch(`${API_BASE}/api/live-inventory-store?${buildModeQuery('live').toString()}`, {
                method: 'POST',
                headers: {
                    'Content-Type': 'text/plain;charset=utf-8'
                },
                body: generateLiveInventoryStoreEditsIni()
            });

            if (!response.ok) {
                const detail = await response.text();
                throw new Error(detail || `HTTP ${response.status}`);
            }
            payload = await response.json();
        }

        clearLocalLiveInventoryDraft();
        state.live_inventory = normalizeLiveInventoryPayload(payload);
        runtime.apiConnected = true;
        runtime.lastMessage = `Saved live inventory adjustments${savingPersistedStoreEdits ? ' and persisted store edits' : ''} to ${state.live_inventory.adjustments_path || 'runtime/strategy_inventory_adjustments.ini'}.`;
    } catch (error) {
        const detail = error instanceof Error ? error.message : String(error);
        runtime.lastMessage = `Unable to save live inventory adjustments: ${detail}.`;
    } finally {
        runtime.pendingLiveInventorySave = false;
        render();
    }
}

async function saveRecorderConfigToWorkspace() {
    const response = await fetchWithTimeout(`${API_BASE}/api/recorder-config`, {
        method: 'POST',
        headers: {
            'Content-Type': 'text/plain;charset=utf-8'
        },
        body: generateRecorderIni()
    }, 15000);

    if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
    }

    const payload = await response.json();
    clearLocalRecorderDraft();
    state.recorder = normalizeRecorderPayload(payload);
    runtime.apiConnected = true;
    runtime.lastMessage = `Saved recorder configuration to ${state.recorder.config_path || 'configs/ctp_md_recorder.ini'}.`;
    render();
}

async function startRecorderRuntime() {
    if (state.mode !== 'live' || runtime.pendingRecorderControlAction) {
        return;
    }

    runtime.pendingRecorderControlAction = true;
    runtime.lastMessage = 'Saving recorder config and launching itrader_ctp_md_recorder.exe...';
    render();

    try {
        await saveRecorderConfigToWorkspace();
        const response = await fetchWithTimeout(`${API_BASE}/api/recorder-run`, {
            method: 'POST'
        }, 20000);
        if (!response.ok) {
            const detail = await response.text();
            throw new Error(detail || `HTTP ${response.status}`);
        }

        const payload = await response.json();
        const recorderRuntime = applyRecorderRuntimePayload(payload);
        runtime.apiConnected = true;
        runtime.lastMessage = payload.message || (recorderRuntime.running
            ? 'Recorder launched successfully.'
            : 'Recorder launch did not start a running process.');
        scheduleRecorderRuntimePoll(recorderRuntime.running ? 1200 : 2500);
    } catch (error) {
        const detail = error instanceof Error ? error.message : String(error);
        runtime.lastMessage = `Unable to launch the recorder: ${detail}.`;
    } finally {
        runtime.pendingRecorderControlAction = false;
        render();
    }
}

async function stopRecorderRuntime() {
    if (state.mode !== 'live' || runtime.pendingRecorderControlAction) {
        return;
    }

    runtime.pendingRecorderControlAction = true;
    runtime.lastMessage = 'Requesting recorder shutdown...';
    render();

    try {
        const response = await fetchWithTimeout(`${API_BASE}/api/recorder-run-stop`, {
            method: 'POST'
        }, 20000);
        if (!response.ok) {
            const detail = await response.text();
            throw new Error(detail || `HTTP ${response.status}`);
        }

        const payload = await response.json();
        const recorderRuntime = applyRecorderRuntimePayload(payload);
        runtime.apiConnected = true;
        runtime.lastMessage = payload.message || (recorderRuntime.running
            ? 'Recorder stop requested.'
            : 'Recorder stopped.');
        scheduleRecorderRuntimePoll(recorderRuntime.running ? 1200 : 2500);
    } catch (error) {
        const detail = error instanceof Error ? error.message : String(error);
        runtime.lastMessage = `Unable to stop the recorder: ${detail}.`;
    } finally {
        runtime.pendingRecorderControlAction = false;
        render();
    }
}

function generateIni() {
    const lines = [];
    lines.push('[platform]');
    lines.push(`mode=${state.mode}`);
    lines.push('');

    const shouldPersistField = (key, value) => key !== 'id' && key !== 'runtime_details' && !key.startsWith('__') && value !== '' && typeof value !== 'object';

    if (state.mode === 'backtest') {
        lines.push('[backtest]');
        const dataDir = String(state.backtest.data_dir ?? '').trim();
        const csvPath = String(state.backtest.csv ?? '').trim();
        if (dataDir) {
            lines.push(`data_dir=${dataDir}`);
        }
        if (csvPath) {
            lines.push(`csv=${csvPath}`);
        }
        lines.push('');
    } else {
        lines.push('[live]');
        lines.push(`environment=${state.live.environment || 'live'}`);
        lines.push(`poll_interval_ms=${state.live.poll_interval_ms}`);
        lines.push(`iterations=${state.live.iterations}`);
        lines.push(`dry_run=${String(state.live.dry_run ?? 'false')}`);
        lines.push(`warmup_enabled=${String(state.live.warmup_enabled ?? 'true')}`);
        lines.push(`warmup_data_dir=${String(state.live.warmup_data_dir ?? '../runtime/ctp_md_recorder/agtick')}`);
        lines.push(`warmup_trading_day=${String(state.live.warmup_trading_day ?? 'auto')}`);
        lines.push('');
    }

    state.accounts.forEach((account) => {
        lines.push(`[account.${account.id}]`);
        Object.entries(account).forEach(([key, value]) => {
            if (shouldPersistField(key, value)) {
                lines.push(`${key}=${value}`);
            }
        });
        lines.push('');
    });

    state.strategies.forEach((strategy) => {
        lines.push(`[strategy.${strategy.id}]`);
        if (strategy.accounts.length > 0) {
            lines.push(`accounts=${strategy.accounts.join(',')}`);
        }
        const forcedStrategyFieldsWritten = new Set();
        Object.entries(strategy).forEach(([key, value]) => {
            if (key === 'accounts' || key === 'account') {
                return;
            }
            const forcedValue = getForcedStrategyFieldValue(key);
            if (forcedValue !== null) {
                if (!forcedStrategyFieldsWritten.has(key)) {
                    lines.push(`${key}=${forcedValue}`);
                    forcedStrategyFieldsWritten.add(key);
                }
                return;
            }
            if (shouldSuppressLegacyStrategyField(strategy, key)) {
                return;
            }
            const effectiveValue = getStrategyFieldValue(strategy, key);
            if (shouldPersistField(key, effectiveValue)) {
                lines.push(`${key}=${effectiveValue}`);
            }
        });
        getStrategySchemaFieldKeys(strategy).forEach((key) => {
            const forcedValue = getForcedStrategyFieldValue(key);
            if (forcedValue === null || forcedStrategyFieldsWritten.has(key)) {
                return;
            }
            lines.push(`${key}=${forcedValue}`);
            forcedStrategyFieldsWritten.add(key);
        });
        lines.push('');
    });

    return lines.join('\n').trim();
}

function renderConfigPreview() {
    configPreview.textContent = generateIni();
}

function renderStatus() {
    const liveRuntime = state.mode === 'live' ? getLiveRuntimeState() : null;
    const liveDryRunEnabled = state.mode === 'live' && isLiveDryRunEnabled();
    let modeLabel = runtime.apiConnected
        ? (state.mode === 'live' ? 'Live API ready' : 'Backtest API ready')
        : (state.mode === 'live' ? 'Live sample ready' : 'Backtest sample ready');
    let defaultCaption = runtime.apiConnected
        ? `Config-backed state loaded from ${runtime.sourceConfig}.`
        : (state.mode === 'live'
            ? 'Trader and market-data connection settings are staged from sample data.'
            : 'Simulation accounts, tick replay data, and sample strategy parameters are staged from sample data.');
    let pillBackground = runtime.apiConnected ? 'rgba(103, 232, 249, 0.18)' : (state.mode === 'live' ? 'rgba(251, 191, 36, 0.16)' : 'rgba(52, 211, 153, 0.16)');
    let pillColor = runtime.apiConnected ? '#b5f8ff' : (state.mode === 'live' ? '#fde68a' : '#a7f3d0');

    if (liveDryRunEnabled) {
        modeLabel = runtime.apiConnected ? 'Dry run armed' : 'Dry-run sample ready';
        pillBackground = 'rgba(251, 191, 36, 0.16)';
        pillColor = '#fde68a';
        defaultCaption = 'Dry run is enabled for this config. Broker submissions are blocked while local paper fills keep entry/exit logic active.';
    }

    if (state.mode === 'live' && runtime.apiConnected && liveRuntime) {
        if (liveRuntime.status === 'running' && liveRuntime.config_matches_request) {
            modeLabel = liveRuntime.stop_requested
                ? (liveDryRunEnabled ? 'Dry run stopping' : 'Live runtime stopping')
                : (liveDryRunEnabled ? 'Dry run running' : 'Live runtime running');
            pillBackground = liveRuntime.stop_requested
                ? 'rgba(251, 191, 36, 0.16)'
                : (liveDryRunEnabled ? 'rgba(251, 191, 36, 0.16)' : 'rgba(52, 211, 153, 0.16)');
            pillColor = liveRuntime.stop_requested
                ? '#fde68a'
                : (liveDryRunEnabled ? '#fde68a' : '#a7f3d0');
            defaultCaption = liveRuntime.message || (liveDryRunEnabled
                ? `Dry run is running for ${liveRuntime.active_config_path || runtime.sourceConfig}; broker submissions are blocked and paper fills are recorded locally.`
                : `itrader.exe is running for ${liveRuntime.active_config_path || runtime.sourceConfig}.`);
        } else if (liveRuntime.status === 'running' && !liveRuntime.config_matches_request) {
            modeLabel = 'Other live runtime running';
            pillBackground = 'rgba(251, 191, 36, 0.16)';
            pillColor = '#fde68a';
            defaultCaption = liveRuntime.message || `Another live runtime is already running for ${liveRuntime.active_config_path || 'a different config'}.`;
        } else if (liveRuntime.status === 'failed' && liveRuntime.config_matches_request) {
            modeLabel = 'Live runtime failed';
            pillBackground = 'rgba(248, 113, 113, 0.16)';
            pillColor = '#fecaca';
            defaultCaption = liveRuntime.message || 'The last live runtime launch failed.';
        }
    }

    const shouldPreferLiveRuntimeCaption = state.mode === 'live'
        && runtime.apiConnected
        && liveRuntime
        && (liveRuntime.status === 'running' || liveRuntime.status === 'failed')
        && /^Loaded live state from local API/.test(runtime.lastMessage || '');

    statusPill.textContent = modeLabel;
    statusPill.style.background = pillBackground;
    statusPill.style.color = pillColor;
    statusCaption.textContent = shouldPreferLiveRuntimeCaption ? defaultCaption : (runtime.lastMessage || defaultCaption);
    renderBacktestProgress();
}

function renderChart() {
    const chart = normalizeChartPayload(state.chart);
    const instrumentEntry = getActiveChartInstrumentEntry(chart);
    const activeAccount = getActiveChartAccount(chart);
    renderChartToolbar(chart, instrumentEntry?.instrument || '', activeAccount);

    if (!instrumentEntry) {
        chartTitle.textContent = 'Bars, indicators and trading signals';
        chartNote.textContent = 'No instrument tabs are available yet.';
        chartStrategyTabs.innerHTML = '';
        destroyChart();
        tradingChart.innerHTML = '<div class="trading-chart__empty">No chart instruments are available yet. Add an instrument to a strategy to populate the chart tabs.</div>';
        return;
    }

    const strategyOptions = getChartStrategyOptions(instrumentEntry.instrument, activeAccount, instrumentEntry.signals, instrumentEntry.indicator_series);
    const activeStrategy = getActiveChartStrategy(instrumentEntry.instrument, activeAccount, instrumentEntry.signals, instrumentEntry.indicator_series);
    renderStrategyTabs(strategyOptions, activeStrategy);

    const visibleSignals = alignSignalsToChartBars(
        filterChartSignals(instrumentEntry.signals, activeAccount, activeStrategy),
        instrumentEntry.bars
    );
    const visibleIndicatorSeries = filterChartIndicatorSeries(instrumentEntry.indicator_series, activeAccount, activeStrategy);
    const strategyLabel = activeStrategy || 'No strategy';
    chartTitle.textContent = `${instrumentEntry.instrument} bars · ${activeAccount === 'all' ? 'All accounts' : activeAccount} · ${strategyLabel}`;
    const indicatorLabel = visibleIndicatorSeries.length > 0
        ? ` Indicators: ${uniqueValues(visibleIndicatorSeries.map((series) => series.label || series.indicator_id)).join(', ')}.`
        : '';
    const defaultChartNote = chart.source === 'backtest-replay'
        ? `Backtest candles are replayed from the configured backtest data source, with ${activeAccount === 'all' ? 'all accounts' : activeAccount} / ${strategyLabel} markers overlaid.`
        : (chart.source === 'backtest-summary'
            ? 'Summary replay kept the UI light by reusing a lightweight synthetic chart preview. Click Run on an attached strategy to compute replay bars and signal markers.'
            : `Signal markers are filtered to ${activeAccount === 'all' ? 'all accounts' : activeAccount} / ${strategyLabel}. Live candles will appear once the detached bar feed is published.`);
    chartNote.textContent = instrumentEntry.warnings[0] || chart.warnings[0] || `${defaultChartNote}${indicatorLabel}`;

    if (!window.LightweightCharts?.createChart) {
        destroyChart();
        tradingChart.innerHTML = '<div class="trading-chart__empty">TradingView Lightweight Charts failed to load. Check network access to the chart library CDN.</div>';
        return;
    }

    if (!Array.isArray(instrumentEntry.bars) || instrumentEntry.bars.length === 0) {
        destroyChart();
        tradingChart.innerHTML = '<div class="trading-chart__empty">No bars are available for the current instrument yet. Replay some data or expose a live bar feed to populate the chart.</div>';
        return;
    }

    const renderKey = buildChartRenderKey(instrumentEntry.instrument, activeAccount, activeStrategy, visibleIndicatorSeries);
    const seriesIdentityKey = buildChartSeriesIdentityKey(visibleIndicatorSeries);
    const hasSeriesConfigChanged = chartRuntime.lastSeriesIdentityKey !== seriesIdentityKey;
    const shouldRecreateChart = !chartRuntime.instance
        || !chartRuntime.candleSeries
        || chartRuntime.indicatorSeries.length !== visibleIndicatorSeries.length
        || hasSeriesConfigChanged;

    if (shouldRecreateChart) {
        destroyChart();
        tradingChart.innerHTML = '';
    }

    const width = Math.max(tradingChart.clientWidth, 320);
    const height = Math.max(tradingChart.clientHeight, 320);
    const { createChart, CandlestickSeries, LineSeries, ColorType, createSeriesMarkers } = window.LightweightCharts;
    if (shouldRecreateChart) {
        const instance = createChart(tradingChart, {
            width,
            height,
            layout: {
                background: { type: ColorType.Solid, color: 'transparent' },
                textColor: '#d7e6ff'
            },
            grid: {
                vertLines: { color: 'rgba(157, 176, 212, 0.10)' },
                horzLines: { color: 'rgba(157, 176, 212, 0.10)' }
            },
            timeScale: {
                borderColor: 'rgba(157, 176, 212, 0.18)',
                timeVisible: true,
                secondsVisible: false
            },
            rightPriceScale: {
                borderColor: 'rgba(157, 176, 212, 0.18)'
            },
            crosshair: {
                vertLine: { color: 'rgba(103, 232, 249, 0.35)' },
                horzLine: { color: 'rgba(103, 232, 249, 0.18)' }
            }
        });

        const candleSeries = instance.addSeries(CandlestickSeries, {
            upColor: 'rgba(0, 0, 0, 0)',
            downColor: '#00e5f0',
            wickUpColor: '#ff3b30',
            wickDownColor: '#00e5f0',
            borderUpColor: '#ff3b30',
            borderDownColor: '#00e5f0',
            borderVisible: true,
            priceLineVisible: true,
            priceLineColor: '#22d3ee',
            lastValueVisible: true
        });

        chartRuntime.instance = instance;
        chartRuntime.candleSeries = candleSeries;
        chartRuntime.indicatorSeries = [];
        chartRuntime.lastRenderKey = renderKey;
        chartRuntime.lastSeriesIdentityKey = seriesIdentityKey;
        chartRuntime.lastBarData = [];
        chartRuntime.lastIndicatorData.clear();
        chartRuntime.lastMarkerKey = '';
        chartRuntime.lastOverlayMarkerKey = '';
        chartRuntime.hasFittedContent = false;
        chartRuntime.lastWidth = width;
        chartRuntime.lastHeight = height;
    }

    const instance = chartRuntime.instance;
    const candleSeries = chartRuntime.candleSeries;
    if (!instance || !candleSeries) {
        return;
    }

    if (chartRuntime.lastWidth !== width || chartRuntime.lastHeight !== height) {
        instance.resize(width, height);
        chartRuntime.lastWidth = width;
        chartRuntime.lastHeight = height;
        chartRuntime.lastOverlayMarkerKey = '';
    }
    chartRuntime.lastBarData = syncChartSeriesData(
        candleSeries,
        instrumentEntry.bars,
        chartRuntime.lastBarData,
        chartBarsEqual,
        cloneChartBars
    );

    const indicatorPalette = ['#f59e0b', '#60a5fa', '#c084fc', '#f472b6', '#22c55e', '#38bdf8'];
    if (LineSeries) {
        if (shouldRecreateChart) {
            visibleIndicatorSeries.forEach((series, index) => {
                if (!Array.isArray(series.points) || series.points.length === 0) {
                    return;
                }
                const lineSeries = instance.addSeries(LineSeries, {
                    color: series.color || indicatorPalette[index % indicatorPalette.length],
                    lineWidth: 2,
                    priceLineVisible: false,
                    lastValueVisible: true,
                    crosshairMarkerVisible: true,
                    title: series.label || series.indicator_id
                });
                chartRuntime.indicatorSeries.push(lineSeries);
            });
        }

        chartRuntime.indicatorSeries.forEach((seriesHandle, index) => {
            const payload = visibleIndicatorSeries[index];
            if (!payload || !Array.isArray(payload.points)) {
                return;
            }
            const seriesKey = chartIndicatorSeriesKey(payload);
            const previousPoints = chartRuntime.lastIndicatorData.get(seriesKey) || [];
            const nextPoints = syncChartSeriesData(
                seriesHandle,
                payload.points,
                previousPoints,
                chartIndicatorPointsEqual,
                cloneChartIndicatorPoints
            );
            chartRuntime.lastIndicatorData.set(seriesKey, nextPoints);
        });
    }

    if (!chartRuntime.hasFittedContent) {
        instance.timeScale().fitContent();
        chartRuntime.hasFittedContent = true;
    }

    const markerPayload = Array.isArray(visibleSignals) ? visibleSignals.map((signal) => ({
        time: signal.time,
        price: signal.price,
        position: signal.position,
        color: signal.color,
        shape: signal.shape,
        text: signal.text
    })) : [];
    renderChartSignalMarkers(instance, candleSeries, markerPayload, createSeriesMarkers);
    subscribeChartMarkerRedraw(instance, candleSeries);
}

function defaultAccount(mode) {
    return mode === 'live'
        ? {
            id: `ctp_${state.accounts.length + 1}`,
            front: 'tcp://127.0.0.1:17001',
            md_front: 'tcp://127.0.0.1:17002',
            broker_id: '',
            user_id: '',
            investor_id: '',
            password: '',
            app_id: '',
            auth_code: '',
            product_info: 'iTrader',
            flow_dir: '',
            md_flow_dir: '',
            production_mode: 'true',
            reconnect_enabled: 'true',
            reconnect_retry_interval_ms: '3000',
            reconnect_max_attempts: '0',
            initial_cash: '0'
        }
        : {
            id: `sim_${state.accounts.length + 1}`,
            initial_cash: '1000000'
        };
}

function defaultStrategy() {
    return {
        id: `strategy_${state.strategies.length + 1}`,
        dll: '../build/Debug/sample_strategy.dll',
        backtest_data_dir: state.mode === 'backtest' ? (state.backtest.data_dir ?? '') : '',
        accounts: [],
        __runtimeStatus: 'stopped',
        __runtimeError: '',
        instruments: state.mode === 'backtest' ? 'AG2602' : 'IF2506',
        fast_window: '4',
        slow_window: '9',
        quantity: '1'
    };
}

function downloadConfig() {
    const blob = new Blob([generateIni()], { type: 'text/plain;charset=utf-8' });
    const link = document.createElement('a');
    link.href = URL.createObjectURL(blob);
    link.download = state.mode === 'live' ? 'live.generated.ini' : 'backtest.generated.ini';
    link.click();
    URL.revokeObjectURL(link.href);
}

async function copyConfig() {
    const text = generateIni();
    await navigator.clipboard.writeText(text);
    const previous = statusCaption.textContent;
    statusCaption.textContent = 'INI copied to clipboard.';
    setTimeout(() => {
        statusCaption.textContent = previous;
    }, 1600);
}

async function postConfigToWorkspace() {
    const response = await fetch(`${API_BASE}/api/config?${buildModeQuery(state.mode).toString()}`, {
        method: 'POST',
        credentials: 'same-origin',
        headers: {
            'Content-Type': 'text/plain;charset=utf-8'
        },
        body: generateIni()
    });

    if (!response.ok) {
        const detail = await response.text();
        throw new Error(detail || `HTTP ${response.status}`);
    }

    const payload = await response.json();
    runtime.apiConnected = true;
    runtime.sourceConfig = payload.config_path ?? defaultConfigPathForMode(state.mode);
    return payload;
}

async function saveConfigToWorkspace() {
    await postConfigToWorkspace();
    runtime.lastMessage = `Saved ${state.mode} configuration to ${runtime.sourceConfig}.`;
    clearLocalStrategyFieldEdits();
    clearLocalStrategiesDraft();
    render();
}

function performRenderNow() {
    const focusState = captureFocusedFieldState();
    renderMetrics();
    renderBacktestSourceSettings();
    renderLiveExecutionSettings();
    renderRecorderSettings();
    renderBacktestPerformance();
    renderAccounts();
    renderStrategies();
    renderConfigPreview();
    renderRuntimeLogPanel();
    renderStatus();
    renderChart();
    renderAccountConfigModal();
    renderStrategyPickerModal();
    renderBacktestCompletionModal();
    document.body.classList.toggle('modal-open', Boolean(getActiveAccountConfig()) || Boolean(getActiveStrategyPickerAccount()) || Boolean(runtime.backtestCompletionVisible));
    restoreFocusedFieldState(focusState);
    revealPendingAccountStrategy();
}

function render(options = {}) {
    const immediate = options && typeof options === 'object' && options.immediate === true;
    if (immediate) {
        if (focusedSelectShouldHoldRender()) {
            deferRenderUntilSelectSettles();
            return;
        }
        renderRuntime.scheduled = false;
        renderRuntime.lastAtMs = Date.now();
        performRenderNow();
        return;
    }

    if (focusedSelectShouldHoldRender()) {
        deferRenderUntilSelectSettles();
        return;
    }

    if (renderRuntime.scheduled) {
        return;
    }

    const now = Date.now();
    const elapsed = now - renderRuntime.lastAtMs;
    const delayMs = Math.max(0, renderRuntime.minIntervalMs - elapsed);
    renderRuntime.scheduled = true;

    const flush = () => {
        if (focusedSelectShouldHoldRender()) {
            renderRuntime.scheduled = false;
            deferRenderUntilSelectSettles();
            return;
        }
        renderRuntime.scheduled = false;
        renderRuntime.lastAtMs = Date.now();
        performRenderNow();
    };

    if (delayMs === 0) {
        window.requestAnimationFrame(flush);
        return;
    }

    window.setTimeout(() => {
        window.requestAnimationFrame(flush);
    }, delayMs);
}

function extractAppAssetVersionFromHtml(html) {
    const match = String(html ?? '').match(/app\.js\?v=([^"'<>\s]+)/);
    return match ? decodeURIComponent(match[1]) : '';
}

function hasPendingLocalConfigChanges() {
    return uiState.localStrategyFieldEdits.size > 0
        || uiState.localStrategiesDraft !== null
        || uiState.localLiveInventoryDraft !== null
        || uiState.localPersistedInventoryDraft !== null
        || uiState.localRecorderDraft !== null;
}

async function checkForUpdatedUiAssets({ reloadWhenChanged = true } = {}) {
    if (!window.location.protocol.startsWith('http')) {
        return false;
    }

    const url = new URL('/index.html', window.location.origin);
    url.searchParams.set('_asset_probe', `${Date.now()}`);
    const response = await fetch(url.toString(), {
        cache: 'no-store',
        credentials: 'same-origin'
    });
    if (!response.ok) {
        return false;
    }

    const latestVersion = extractAppAssetVersionFromHtml(await response.text());
    if (!latestVersion || latestVersion === APP_ASSET_VERSION) {
        return false;
    }

    if (!reloadWhenChanged) {
        runtime.lastMessage = 'A newer dashboard UI is available. Save or refresh the page to load it.';
        render();
        return true;
    }

    const reloadUrl = new URL(window.location.href);
    reloadUrl.searchParams.set('_ui_reload', latestVersion);
    window.location.replace(reloadUrl.toString());
    return true;
}

function startUiAssetVersionWatcher() {
    checkForUpdatedUiAssets().catch(() => {});
    window.setInterval(() => {
        checkForUpdatedUiAssets().catch(() => {});
    }, 60000);
}

window.addEventListener('resize', () => {
    if (!chartRuntime.instance) {
        return;
    }

    const width = Math.max(tradingChart.clientWidth, 320);
    const height = Math.max(tradingChart.clientHeight, 320);
    if (chartRuntime.lastWidth === width && chartRuntime.lastHeight === height) {
        return;
    }

    chartRuntime.instance.resize(width, height);
    chartRuntime.lastWidth = width;
    chartRuntime.lastHeight = height;
    chartRuntime.lastOverlayMarkerKey = '';
    renderPriceCoordinateTradeMarkers(chartRuntime.instance, chartRuntime.candleSeries, chartRuntime.currentMarkerPayload);
});

window.addEventListener('itrader:chart-library-ready', () => {
    runtime.lastMessage = runtime.apiConnected
        ? `Loaded ${state.mode} state from local API at ${runtime.apiBase}. Chart library is ready.`
        : 'Chart library loaded. Sample dashboard rendering is now fully interactive.';
    render();
});

window.addEventListener('itrader:chart-library-error', () => {
    runtime.lastMessage = 'TradingView chart library could not be loaded from the CDN. The rest of the dashboard remains available.';
    render();
});

document.querySelectorAll('#mode-switch .segmented__item').forEach((button) => {
    button.addEventListener('click', () => switchMode(button.dataset.mode));
});

document.querySelectorAll('.nav__item').forEach((button) => {
    button.addEventListener('click', () => setActiveSection(button.dataset.section));
});

document.getElementById('load-sample-button').addEventListener('click', () => {
    hydrateMode(state.mode).catch(() => {
        runtime.lastMessage = 'Refresh failed; keeping the current dashboard state.';
        render();
    });
});
document.getElementById('download-config-button').addEventListener('click', downloadConfig);
document.getElementById('save-config-button').addEventListener('click', () => {
    saveConfigToWorkspace().catch(() => {
        statusCaption.textContent = 'Saving to the workspace failed. Check whether the local UI API server is running.';
    });
});
if (saveRecorderConfigButton) {
    saveRecorderConfigButton.addEventListener('click', () => {
        saveRecorderConfigToWorkspace().catch(() => {
            statusCaption.textContent = 'Saving the recorder INI failed. Check whether the local UI API server is running.';
        });
    });
}
if (toggleRecorderPanelButton) {
    toggleRecorderPanelButton.addEventListener('click', () => {
        uiState.recorderPanelCollapsed = !uiState.recorderPanelCollapsed;
        render();
    });
}
if (startRecorderButton) {
    startRecorderButton.addEventListener('click', () => {
        startRecorderRuntime().catch(() => {
            statusCaption.textContent = 'Starting the recorder failed. Check whether the local UI API server is running.';
        });
    });
}
if (stopRecorderButton) {
    stopRecorderButton.addEventListener('click', () => {
        stopRecorderRuntime().catch(() => {
            statusCaption.textContent = 'Stopping the recorder failed. Check whether the local UI API server is running.';
        });
    });
}
cancelBacktestButton.addEventListener('click', cancelBacktestRunWait);
document.getElementById('copy-config-button').addEventListener('click', () => {
    copyConfig().catch(() => {
        statusCaption.textContent = 'Clipboard copy failed. Use the generated INI panel manually.';
    });
});
document.getElementById('add-account-button').addEventListener('click', () => {
    state.accounts.push(defaultAccount(state.mode));
    render();
});
strategyCatalogButton.addEventListener('click', () => {
    loadStrategyFileCatalog({ force: true })
        .finally(() => {
            render();
        });
});
if (strategyUploadButton && strategyUploadInput) {
    strategyUploadButton.addEventListener('click', () => {
        if (runtime.pendingStrategyUpload) {
            return;
        }
        strategyUploadInput.click();
    });
    strategyUploadInput.addEventListener('change', () => {
        const file = strategyUploadInput.files?.[0] ?? null;
        if (!file) {
            return;
        }
        uploadStrategyDllFile(file).catch((error) => {
            const detail = error instanceof Error ? error.message : String(error);
            runtime.lastMessage = `Strategy DLL upload failed: ${detail}`;
            runtime.pendingStrategyUpload = false;
            strategyUploadInput.value = '';
            render();
        });
    });
}
document.getElementById('account-config-close-button').addEventListener('click', closeAccountConfig);
document.getElementById('account-config-done-button').addEventListener('click', closeAccountConfig);
document.querySelectorAll('[data-action="close-account-config"]').forEach((element) => {
    element.addEventListener('click', closeAccountConfig);
});
document.getElementById('strategy-picker-close-button').addEventListener('click', closeStrategyPicker);
document.getElementById('strategy-picker-done-button').addEventListener('click', closeStrategyPicker);
document.querySelectorAll('[data-action="close-strategy-picker"]').forEach((element) => {
    element.addEventListener('click', closeStrategyPicker);
});
document.getElementById('backtest-done-close-button').addEventListener('click', closeBacktestCompletionNotice);
backtestDoneOkButton.addEventListener('click', closeBacktestCompletionNotice);
document.querySelectorAll('[data-action="close-backtest-done"]').forEach((element) => {
    element.addEventListener('click', closeBacktestCompletionNotice);
});
document.addEventListener('keydown', (event) => {
    if (event.key === 'Escape' && !backtestDoneModal.classList.contains('hidden')) {
        closeBacktestCompletionNotice();
        return;
    }

    if (event.key === 'Escape' && !strategyPickerModal.classList.contains('hidden')) {
        closeStrategyPicker();
        return;
    }

    if (event.key === 'Escape' && !accountConfigModal.classList.contains('hidden')) {
        closeAccountConfig();
    }
});
document.addEventListener('focusout', (event) => {
    if (event.target instanceof HTMLSelectElement) {
        window.setTimeout(flushDeferredSelectRender, 0);
    }
}, true);
document.addEventListener('change', (event) => {
    if (event.target instanceof HTMLSelectElement) {
        allowFocusedSelectRenderBriefly();
    }
}, true);

setActiveSection('overview');
startUiAssetVersionWatcher();
hydrateMode(pageParams.get('mode') === 'live' ? 'live' : 'backtest').catch(() => {
    applyState(structuredClone(sampleStates.backtest));
    runtime.apiConnected = false;
    runtime.sourceConfig = 'sample-state';
    runtime.lastMessage = 'Initialized with fallback sample data.';
    render();
});
