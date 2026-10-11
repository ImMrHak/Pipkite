// the other languages are typed against this one.
// *Html strings can contain markup; href="/..." links get localized when rendered.
import { BENCH, DOWNLOADS, VERSION } from '../config';
import { num } from './format';

const L = 'en' as const;
const ms = num(L, BENCH.ownOverheadP50Ms, 1);
const p95 = num(L, BENCH.ownOverheadP95Ms, 1);

export const en = {
  meta: {
    homeTitle: 'Free Telegram to MT5 Signal Copier | Pipkite',
    homeDesc:
      'Copy Telegram trading signals to MetaTrader 5 automatically in about 4 ms. Free Windows app with parsers built for you, AI review and one-click pause.',
    downloadTitle: `Download Pipkite ${VERSION} – Free Telegram MT5 Copier`,
    downloadDesc: `Download Pipkite ${VERSION} for Windows 10/11: a free Telegram to MetaTrader 5 signal copier. Installer or portable zip, with SHA-256 checksums.`,
    licenseTitle: 'Get a Free Pipkite License Key (No Card, Never Expires)',
    licenseDesc:
      'Generate a free Pipkite license key in seconds. It never expires and unlocks parsers for your channels, the AI review and signed automatic updates.',
    guideTitle: 'How to Copy Telegram Signals to MT5 Automatically (2026)',
    guideDesc:
      'Step-by-step guide to copy Telegram trading signals to MetaTrader 5 automatically: connect Telegram and MT5, get parsers, set risk rules, test safely.',
    changelogTitle: `Pipkite Changelog – What's New in ${VERSION}`,
    changelogDesc: `Release notes for Pipkite, the free Telegram to MT5 copier. Version ${VERSION}: execution engine, parsers, AI review, speed work, dashboard and security.`,
    privacyTitle: 'Privacy – What Pipkite Collects and Why',
    privacyDesc:
      'What Pipkite keeps on your PC, what the license, AI review and custom parser services receive, and what is never collected. Plain language.',
    notFoundTitle: 'Page not found | Pipkite',
    notFoundDesc: "This page doesn't exist. Head back to Pipkite, the free Telegram to MT5 signal copier.",
    ogAlt: `Pipkite: Telegram signals to MetaTrader 5 in ${ms} ms`,
    appDescription:
      'Free Windows app that copies trading signals from Telegram channels and groups to MetaTrader 5. Each channel gets a parser built by the Pipkite team; signals are checked against your risk rules and placed in about 4 ms.',
    featureList: [
      'Copies Telegram channel and group signals to MetaTrader 5 automatically',
      `About ${ms} ms from Telegram message to order sent (median, measured)`,
      'Parsers built for each channel by the Pipkite team, delivered as automatic updates',
      'Advisory AI review of every signal with Good, Bad or Uncertain verdict and reasons',
      'Fixed-lot or risk-percent position sizing, spread and slippage caps, trading hours',
      'Waits for entry ranges and can force-enter trades that slip just past them',
      'Multiple take-profits, split targets',
      'One-click pause and resume of copying',
      'Crash-safe order journal with reconciliation',
      'Live profit and latency dashboard',
    ],
  },

  common: {
    skip: 'Skip to content',
    home: 'Home',
    download: 'Download',
    freeKey: 'Free key',
    downloadWindows: 'Download for Windows',
    getFreeKey: 'Get a free license key',
    getYourFreeKey: 'Get your free key',
    copy: 'Copy',
    copied: 'Copied',
    themeToggle: 'Switch color theme',
    openMenu: 'Open menu',
    language: 'Language',
    mainNav: 'Main',
    mobileNav: 'Mobile',
    breadcrumb: 'Breadcrumb',
    onThisPage: 'On this page',
    illustration: 'Illustration',
    homeAria: 'Pipkite home',
  },

  nav: { how: 'How it works', features: 'Features', speed: 'Speed', ai: 'AI', guide: 'Guide', faq: 'FAQ' },

  footer: {
    tagline: 'Free Telegram-to-MetaTrader 5 signal copier for Windows, with parsers built for your channels.',
    versionLine: (v: string) => `Version ${v} · Windows 10 / 11, 64-bit`,
    product: 'Product',
    help: 'Help',
    features: 'Features',
    performance: 'Performance',
    changelog: 'Changelog',
    guide: 'How to copy Telegram signals to MT5',
    faq: 'FAQ',
    privacy: 'Privacy',
    riskTitle: 'Risk notice.',
    risk: 'Trading leveraged products such as forex and CFDs carries a high risk of loss. Pipkite copies signals; it cannot guarantee fills or profits, and AI reviews are an early version that can be wrong. Test on a demo account first.',
    legal: (year: number) =>
      `© ${year} Pipkite. MetaTrader is a trademark of MetaQuotes Ltd. Telegram is a trademark of Telegram FZ-LLC. Pipkite is not affiliated with either.`,
  },

  hero: {
    badgeNew: 'New',
    badge: (v: string) => `Version ${v} is out. Free for Windows`,
    title: 'Telegram signals to MetaTrader 5 in',
    unit: 'ms',
    lead: 'Pipkite reads the channels you choose, understands every signal, checks it against your rules, and places the order in MT5 before you could even unlock your phone.',
    leadStrong: '100% free.',
    checks: ['Free forever, no card', 'Parsers built for you', 'No admin rights needed', 'Windows 10 / 11'],
    shotAlt: 'The Pipkite dashboard: connections, account and profit, live feed and latency',
    cardNow: 'now',
    aiReview: 'AI review',
    good: 'Good',
    rr: 'reward/risk 2.2',
    orderSent: 'Order sent to MT5',
    downloadsCount: '{n} downloads',
  },

  how: {
    eyebrow: 'How it works',
    title: 'From message to order without lifting a finger',
    lead: "No copy-pasting prices, no missed entries while you sleep. Here's what happens when a signal arrives.",
    step1: '1 · Message arrives',
    step2: '2 · Understood and checked',
    step3: '3 · Order in MetaTrader 5',
    fields: { symbol: 'Symbol', side: 'Side', entry: 'Entry', entryNote: 'wait for range', sl: 'Stop-loss', tps: 'Take-profits' },
    aiGood: 'AI: Good',
    checksOk: 'Spread OK · Risk OK',
    volume: 'Volume',
    sent: 'Sent',
    illustration: 'Illustration. Sizing, targets and entry handling follow your own rules.',
    setupTitle: 'Set up once, with a guided wizard',
    steps: [
      { title: 'Connect Telegram', text: 'Sign in with your own Telegram account and tick the channels or groups to copy. Login codes are never stored.' },
      { title: 'Connect MetaTrader 5', text: 'Click "Start MetaTrader". Pipkite installs its Expert Advisor, opens your terminal and pairs with it.' },
      { title: 'Request your parsers', text: 'One click per channel. Our team builds a parser for that channel’s exact format and it installs automatically, no restart.' },
      { title: 'Set your rules', text: 'Lot size or risk %, max spread, trading hours, entry ranges. Copying runs once MT5 is paired; pause or resume in one click.' },
    ],
  },

  features: {
    eyebrow: 'Features',
    title: 'Everything a serious copier needs',
    lead: 'Native C++ on Windows. No browser extension, no cloud relay between you and your broker, no monthly bill.',
    parserTitle: 'Understands messy signals',
    parserText:
      'Emojis, Markdown, Arabic-Indic or superscript digits, odd dashes, several take-profits. Each channel gets a parser built by our team for its format; it normalizes the message, maps every value back to the original text, and refuses to guess on commentary, edits or "TP1 hit".',
    legend: { symbol: 'symbol', side: 'side', label: 'label', ignored: 'ignored' },
    entry: 'entry',
    valid: 'Valid',
    rangeTitle: 'Waits for your entry',
    rangeText: 'For "buy 4299–4295" it waits until price enters the range, reacting on every quote. Force-enter can catch trades that slip just past it.',
    filled: 'filled',
    small: [
      { title: 'Executes on your terms', text: 'Fixed lots or risk-percent sizing (always rounded down), spread and slippage caps, trading hours, account limits, split take-profits.' },
      { title: 'Crash-safe by design', text: 'Every order is journaled to disk before it leaves. After a crash or power cut, unknown orders are reconciled, never blindly re-sent.' },
      { title: 'Pause and resume in one click', text: 'Copying runs as soon as MT5 is paired. One click stops new trades instantly and resumes them just as fast. Pausing never closes open trades.' },
      { title: 'Parsers built for you', text: 'Nothing to configure: request a parser for a channel and our team builds it for that exact format. It arrives as a signed update and loads with no restart.' },
    ],
    dashTitle: 'A live dashboard that tells the truth',
    dashText:
      'Balance, equity, floating and realized P/L net of fees, a live feed of every signal and what happened to it, and latency percentiles measured on your own PC. Broker data is authoritative.',
    equity: 'Equity',
    floating: 'Floating P/L',
    latency: 'Latency p95',
  },

  speed: {
    eyebrow: 'Speed',
    title: 'Measured, not promised.',
    leadHtml: `In copy trading, milliseconds are price. Pipkite's whole path from the Telegram message to the order leaving for your broker runs in native code with a single durable write, and it takes about <strong>${ms} ms</strong>.`,
    proof: "You don't have to take our word for it: the Dashboard shows live latency percentiles measured on your own PC.",
    stats: [
      { label: 'Telegram → MT5 send', note: `Pipkite's own time, median (p95 ${p95} ms)` },
      { label: 'To read a signal', note: `median parse time, p95 ${num(L, BENCH.parseP95Us, 1)} µs` },
      { label: 'Signals per second', note: 'offline parser throughput' },
      { label: 'Disk flush per order', note: `down from ${BENCH.flushesBefore}, still crash-safe` },
    ],
    chartTitle: 'Telegram message → order sent to MT5',
    chartSub: 'Median, milliseconds. Shorter is better.',
    fastest: 'Fastest tested',
    rowUs: 'Pipkite 1.0',
    rowBefore: 'Before optimisation',
    improvementStrong: '3.6× faster than our own first build.',
    improvement: 'One disk flush per order instead of four, critical writes jump the queue, and entry ranges react on every quote instead of on a 100 ms timer.',
    reference: `Reference machine: ${BENCH.machine}, Release build, durable journal on. Telegram delivery, your broker and your network add their own time on top.`,
  },

  ai: {
    exampleLabel: 'Example AI review',
    reasons: ['Stop-loss is on the correct side of the entry', 'Reward/risk to the last target is 2.2 : 1', 'First target is closer than the stop-loss'],
    good: 'Good',
    eyebrow: 'AI included',
    title: 'A second opinion on every single signal, free',
    lead: "Pipkite's AI backend is already running. Nothing to set up and no API key to buy: add your free license key and every signal gets a second opinion.",
    points: [
      { title: 'Clear verdicts with reasons', text: 'Good, Bad or Uncertain, plus the reasons behind it: stop placement, reward/risk and entry width.' },
      { title: 'Advice, not orders', text: 'The verdict appears next to each signal in the live feed to help you judge a provider. Turn it on or off in Execution policy.' },
      { title: 'Only the signal is sent', text: 'Symbol, side, entry, SL and TPs. Never your account, balance, broker login, chat or message text. Signals are kept to help the AI learn.' },
    ],
    warnStrong: 'Early days: the AI can be wrong.',
    warn: "It's a first version, so it can miss things, and its score is a confidence rating, not a promise of profit. We're improving it with every release; treat it as a second opinion, never as a guarantee.",
  },

  screens: {
    eyebrow: 'Take a look',
    title: 'Clean, calm and easy to read',
    lead: "Dark and light themes (switch this site's theme to see both), a tooltip on every button, and a full user guide built in.",
    tablist: 'App screens',
    screenAlt: (label: string, text: string) => `Pipkite ${label} screen. ${text}`,
    items: {
      dashboard: { label: 'Dashboard', text: 'Connections, account and profit, the live feed and latency, all on one screen.' },
      history: { label: 'History', text: 'Every message, how it was read and what was done, with the reason. Export to CSV or JSON.' },
      metatrader: { label: 'MetaTrader', text: 'Start MT5 with the EA attached, approve the pairing and pick each channel’s symbol.' },
      'execution-policy': { label: 'Execution policy', text: 'Sizing, take-profit handling, timing and limits. Every option has a "?" that explains it.' },
      'signal-rules': { label: 'Signal rules', text: 'See which parser reads each channel, and request a custom one in a click.' },
    },
    note: 'The app interface is in English.',
  },

  free: {
    eyebrow: 'Pricing',
    title: 'Free. Not a trial, not freemium.',
    lead: 'No subscription, no card, no "pro" tier hiding the good parts. Download it, get your free key, and use everything.',
    price: '$0',
    forever: 'forever',
    included: [
      'The full desktop app, no feature limits',
      'Unlimited Telegram channels and signals',
      'Parsers built for your channels',
      'AI review on every signal',
      'Signed automatic updates',
      'Live profit and latency dashboard',
    ],
    privacyEyebrow: 'Privacy and safety',
    privacyTitle: "Free doesn't mean you're the product",
    privacy: [
      { title: 'Your data stays on your PC', text: 'History, settings and journals live in a local database. Secrets are encrypted with Windows DPAPI.' },
      { title: 'No broker passwords, ever', text: 'Your broker login stays inside MetaTrader. Pipkite never asks for it.' },
      { title: 'Signed modules and updates', text: 'Every parser module and release is ECDSA-signed and verified before it loads.' },
    ],
  },

  faq: {
    eyebrow: 'FAQ',
    title: 'Questions, answered',
    leadHtml:
      'New to copying signals? Read the <a href="/telegram-to-mt5-copier">step-by-step guide</a>. The app also ships with a full user guide with screenshots of every screen.',
    items: [
      {
        q: 'What is Pipkite?',
        a: `Pipkite is a free Windows app that copies trading signals from Telegram channels and groups to MetaTrader 5. It reads each new message with a parser built for that channel, extracts the symbol, direction, entry, stop-loss and take-profits, checks them against your risk rules, and places the order in your own MT5 terminal in about ${ms} ms. An optional AI review gives a second opinion on each signal.`,
      },
      {
        q: 'Is Pipkite really free?',
        a: 'Yes. The app, the parsers, the AI review and updates are all free, with no subscription, trial or card. The license key is free too; it identifies your install to the parser, AI and update services.',
      },
      {
        q: 'How do I copy Telegram signals to MT5 automatically?',
        a: 'Install Pipkite, sign in with your own Telegram account and tick the channels to copy, click "Start MetaTrader" to attach the Pipkite Expert Advisor and approve the pairing, request a parser for each channel, and set your lot size or risk percentage. Once a channel’s parser is installed, its new signals are placed in MT5 on their own. Pause or resume copying with one click.',
      },
      {
        q: 'How fast is it, honestly?',
        a: `Pipkite's own part, from Telegram handing over the message to the order being sent to MT5, takes about ${ms} ms (median) on the reference PC. Telegram's delivery, your broker and your internet connection add their own time. The Dashboard shows the latency measured on your machine.`,
      },
      {
        q: 'Does it work with my broker?',
        a: 'Pipkite places orders through an Expert Advisor inside your own MetaTrader 5 desktop terminal on Windows, so it works with brokers you can trade with in MT5. Your broker login stays inside MetaTrader. Map each channel symbol (for example GOLD) to your broker’s name for it (for example XAUUSD).',
      },
      { q: 'Does it work with MetaTrader 4?', a: 'No. Pipkite supports MetaTrader 5 only.' },
      {
        q: 'Can it copy from private channels and groups?',
        a: 'Yes. Pipkite signs in to Telegram as you, so it can read any channel or group your account is a member of, including private ones. Only messages that arrive after you sign in are copied; old history never becomes orders.',
      },
      {
        q: 'Can I try it without risking money?',
        a: 'Yes. Connect a demo MT5 account first: copying works exactly the same with nothing at stake. The setup wizard also has a "Test a sample" step that shows what Pipkite reads from a pasted message without sending anything. Copying is live once MT5 is paired, and Pause stops new trades instantly.',
      },
      {
        q: 'Do I have to set up or write a parser?',
        a: 'No, and you can’t: parsers are built by the Pipkite team. Click "Request a custom parser" on the Signal rules screen; a few recent messages from that channel are sent with links, @handles, phone numbers and emails removed, we build a parser for its exact format, and it installs automatically as a signed update without a restart. Until a channel’s parser arrives, its messages aren’t traded.',
      },
      {
        q: 'Is the AI review always right?',
        a: 'No. The AI review is an early version and it can be wrong. It weighs how each signal is built (stop placement, reward/risk, entry width) and gives a confidence score, which is not a promise of profit. It’s shown as advice next to each signal, and we’re improving it over time.',
      },
      {
        q: 'Does the AI see my account?',
        a: 'No. Only the signal fields are sent: symbol, side, order type, entry, stop-loss and take-profits. Never your account number, balance, broker login, chat or message text. Nothing about the signal is kept after the answer.',
      },
      {
        q: 'Which languages are supported?',
        a: 'This website is available in English, French, Spanish and Arabic. The Pipkite app itself is currently in English. Parsers work with signals written in any language, because each one is built for its channel.',
      },
      {
        q: 'Does my PC need to stay on?',
        a: 'Yes. Pipkite and MetaTrader run on your Windows PC, so sleep, shutdown or a closed MT5 terminal pause copying. Pipkite can keep Windows awake while copying; for 24/7 copying, run it on a Windows VPS.',
      },
      {
        q: 'Windows shows a SmartScreen warning. Is that normal?',
        a: 'This first release is not signed with a paid code-signing certificate yet, so Windows may warn you. Choose "More info → Run anyway". You can verify the file against the SHA-256 checksum on the download page.',
      },
    ],
  },

  cta: {
    title: 'Stop missing entries. Start copying in milliseconds.',
    lead: 'Free download, free key, and a guided setup wizard.',
  },

  download: {
    breadcrumb: 'Download',
    h1: 'Download the free Telegram to MT5 copier',
    sub: (v: string) => `Pipkite ${v} for Windows 10 and 11. Released`,
    whatsNew: "What's new",
    installer: { title: 'Installer', badge: 'Recommended', text: 'Installs just for you, with no admin rights needed. Adds a Start-menu shortcut and the user guide.' },
    portable: { title: 'Portable zip', text: 'Unzip anywhere you can write and run Pipkite.Launcher.exe. Nothing is installed.' },
    sha: 'SHA-256',
    checkHint: 'Check a download in PowerShell:',
    stepsTitle: 'Up and running in three steps',
    steps: [
      { title: 'Run the installer', text: `Open ${DOWNLOADS.installer.name}. If SmartScreen appears, choose "More info → Run anyway".` },
      { title: 'Add your free key', text: 'The welcome wizard asks for your license key. Paste it and click "Validate & save".' },
      { title: 'Follow the wizard', text: 'Connect Telegram, start MetaTrader, pick your channels, request their parsers and test a sample signal.' },
    ],
    noKey: "Don't have a key yet? Get one free",
    reqTitle: 'System requirements',
    req: [
      'Windows 11 or Windows 10 22H2, 64-bit',
      '2+ CPU cores, 4 GB RAM, SSD recommended',
      'MetaTrader 5, logged in to your broker',
      'A Telegram account',
      '.NET Framework 4.8 (built into Windows 10/11)',
    ],
    reqNote: 'Sleep, shutdown or a closed MT5 terminal pause copying. For 24/7 copying, run it on a Windows VPS. The app interface is in English.',
  },

  license: {
    breadcrumb: 'Free license key',
    h1: 'Your key, free, and it never expires',
    lead: "No payment and no card. The key links your install to Pipkite's online services:",
    unlocks: ['Parsers built for your channels', 'AI review on every signal', 'Signed automatic updates'],
    skipHtml: 'The app opens without a key, but parsers, the AI review and updates need one. Add it in the welcome wizard or later under <strong>Settings → AI and licensing</strong>.',
    name: 'Your name',
    namePh: 'Jane Trader',
    email: 'Email',
    emailOpt: '(optional, we’ll email you the key)',
    emailPh: 'you@example.com',
    submit: 'Generate my free key',
    generating: 'Generating…',
    detailsUse: 'We only use your details to identify your key.',
    privacy: 'Privacy',
    ready: 'Your license key is ready',
    copyKey: 'Copy key',
    saveIt: "Save it somewhere safe. It's shown only once.",
    emailSentHtml: 'We also sent it to <strong></strong>. Check your spam folder if it isn’t there in a few minutes.',
    emailNone: 'You didn’t enter an email, so this is the only copy. If you lose it, you’ll have to generate a new key.',
    done: 'Done',
    afterHtml: [
      'Open Pipkite (or <a href="/download">download it</a>).',
      'Paste the key in the welcome wizard, or in Settings → AI and licensing.',
      'Click <strong>Validate &amp; save</strong>. The status turns green.',
    ],
    errors: {
      name: 'Please enter your name.',
      email: 'That email address doesn’t look right.',
      bad: 'Please check your details and try again.',
      rate: 'Too many requests. Please wait a minute and try again.',
      server: 'The license service is having trouble. Please try again in a moment.',
      unexpected: 'The license service returned an unexpected answer.',
      network: 'Couldn’t reach the license service. Check your connection and try again.',
    },
  },

  guide: {
    breadcrumb: 'Telegram to MT5 guide',
    h1: 'How to copy Telegram signals to MetaTrader 5 automatically',
    lead: 'A complete, practical guide: from a signal posted in a Telegram channel to an order in your MT5 terminal, without copying prices by hand.',
    updated: 'Updated',
    by: 'By the Pipkite team',
    readTime: '7 min read',
    shortTitle: 'Short answer',
    short: `To copy Telegram signals to MT5 automatically, install a Telegram-to-MT5 copier such as Pipkite, sign in with your Telegram account, choose the channels to copy, attach the copier's Expert Advisor to your MetaTrader 5 terminal, request a parser for each channel and set your lot size or risk percentage. Each new signal is then parsed and placed as an order on its own, in about ${ms} ms. One click pauses or resumes copying.`,
    figures: {
      metatrader: { alt: 'Pipkite MetaTrader screen with the terminal list, Start MetaTrader button and pairing status', caption: 'The MetaTrader screen: start the terminal, approve the pairing, map symbols.' },
      'signal-rules': { alt: 'Pipkite Signal rules screen listing channels, their parser status and the Request a custom parser button', caption: "Signal rules: each channel's parser, and the button to request one." },
      'execution-policy': { alt: 'Pipkite Execution policy screen with position sizing, take-profit and timing settings', caption: 'Execution policy: how big, which targets, and when not to trade.' },
    },
    sections: [
      {
        id: 'what-is',
        title: 'What is a Telegram to MT5 copier?',
        html: `<p>A Telegram to MT5 copier is software that watches Telegram channels or groups for trading signals and turns each one into an order in MetaTrader 5. A signal such as "GOLD BUY 4299–4295, SL 4282, TP 4307" is read, the symbol, direction, entry, stop-loss and take-profits are extracted, your own rules decide the size, and the order is sent to your broker through MT5.</p>
<p>Copying by hand is slow and error-prone: by the time you've unlocked your phone, read the message and typed the order, the price has often moved. A copier removes that delay and the typos, and keeps working while you sleep.</p>`,
      },
      {
        id: 'need',
        title: 'What you need',
        html: `<ul>
<li>A Windows 10 (22H2) or Windows 11 PC, 64-bit, with 4 GB of RAM. A Windows VPS works for 24/7 copying.</li>
<li>MetaTrader 5 (desktop) logged in to your broker. A demo account is best for the first run.</li>
<li>Your Telegram account, and membership of the channels or groups you want to copy.</li>
<li>A free Telegram API ID and hash from <code>my.telegram.org</code> (takes a minute).</li>
<li>Pipkite and a <a href="/license">free license key</a>, which you need to request parsers and receive them as updates (and for the AI review).</li>
</ul>`,
      },
      {
        id: 'step-1',
        title: 'Step 1: Install Pipkite',
        html: `<p><a href="/download">Download the installer</a> and run it. It installs just for your Windows user, with no admin rights needed. If Windows SmartScreen appears, choose <strong>More info → Run anyway</strong>; you can check the file against the SHA-256 checksum on the download page. On first launch a short wizard walks you through every step below, with a live status line for each. The app interface is in English.</p>`,
      },
      {
        id: 'step-2',
        title: 'Step 2: Connect Telegram',
        html: `<ol>
<li>Go to <code>my.telegram.org</code>, open <strong>API development tools</strong> and copy the API ID and API hash.</li>
<li>Paste both into Pipkite and click <strong>Save credentials</strong>. They're encrypted on your PC with Windows DPAPI and never logged.</li>
<li>Enter your phone number, click <strong>Send code</strong> and type the code Telegram sends you (and your two-step password if you use one).</li>
<li>Tick the channels and groups to copy and click <strong>Add checked as sources</strong>. Private channels work too, as long as your account is a member.</li>
</ol>
<p>Only messages that arrive after you sign in are copied. Old history never becomes orders.</p>`,
      },
      {
        id: 'step-3',
        title: 'Step 3: Connect MetaTrader 5',
        figure: 'metatrader',
        html: `<ol>
<li>On the <strong>MetaTrader</strong> screen, pick your terminal and click <strong>Start MetaTrader</strong>. Pipkite installs its Expert Advisor, opens MT5 with it on one chart and enables the settings it needs. It asks first.</li>
<li>Log in to your broker inside MetaTrader as usual. Pipkite never asks for your broker password.</li>
<li>Back in Pipkite, click <strong>Approve pairing</strong> and type your account number to confirm. Pair a <strong>demo account</strong> first: once an account is paired, copying is live.</li>
<li>Map each channel's symbol to your broker's name for it, for example a channel's "GOLD" to your broker's "XAUUSD".</li>
</ol>`,
      },
      {
        id: 'step-4',
        title: 'Step 4: Request a parser for each channel',
        figure: 'signal-rules',
        html: `<p>Every provider writes signals differently, so each channel needs a <em>parser</em> built for its format. Pipkite ships with none, and you don't build or edit them yourself: the Pipkite team does.</p>
<ol>
<li>Open <strong>Signal rules</strong>, select a channel and click <strong>Request a custom parser for this channel</strong>. Add a note if you like.</li>
<li>Pipkite sends a few recent messages from that channel, with links, @handles, phone numbers and emails removed.</li>
<li>We build the parser and publish it as a signed update. It installs automatically and starts working without a restart; <strong>Check for parser updates</strong> pulls it right away.</li>
</ol>
<p>Until a channel's parser arrives, its messages are shown as unsupported and never traded.</p>`,
      },
      {
        id: 'step-5',
        title: 'Step 5: Set your risk rules',
        figure: 'execution-policy',
        html: `<ul>
<li><strong>Position size:</strong> a fixed lot size, or a percentage of equity risked at the stop-loss. Risk sizing always rounds down to your broker's lot step; if even the minimum lot is too much, the trade is skipped rather than forced.</li>
<li><strong>Take-profits:</strong> use one target, or split the position across several.</li>
<li><strong>Limits:</strong> maximum signal age, maximum spread and slippage per symbol, open positions, daily loss and trading hours.</li>
<li><strong>Entry ranges:</strong> for "buy 4299–4295" signals, Pipkite waits for price to enter the range. Optional <em>force-enter</em> takes the trade if price slipped just past it by a few pips you choose.</li>
<li><strong>AI review:</strong> turn on "review every signal" to see a Good / Uncertain / Bad opinion next to each signal in the live feed.</li>
</ul>`,
      },
      {
        id: 'step-6',
        title: 'Step 6: Test, then let it copy',
        html: `<p>There's no separate preview mode: once MT5 is paired and a channel has its parser, valid signals are copied. So test before risking money:</p>
<ul>
<li>In the setup wizard's <strong>Test a sample</strong> step, paste a real message from the channel to see exactly what Pipkite reads from it. Nothing is sent.</li>
<li>Run on a <strong>demo account</strong> first and watch a few real signals on the Dashboard and in History. Each row explains its decision, so a rejected signal tells you why (no stop-loss, symbol not mapped, spread too wide and so on).</li>
<li>Use <strong>Pause copying</strong> in the top bar (or the tray icon) to stop new trades instantly, and <strong>Resume copying</strong> to start again. Pausing never closes open trades.</li>
</ul>`,
      },
      {
        id: 'formats',
        title: 'Which signal formats work?',
        html: `<p>Practically any format a provider uses consistently, in any language, because each channel's parser is built for its own style. Parsers handle emojis, Markdown, unusual dashes, Arabic-Indic and superscript digits and multiple take-profits, and ignore commentary like "TP1 hit" instead of guessing. If a provider changes how they post, request the parser again and the updated one arrives automatically.</p>`,
      },
      {
        id: 'ai',
        title: 'What does the AI review do?',
        html: `<p>The AI review gives a second opinion on each signal: <strong>Good</strong>, <strong>Uncertain</strong> or <strong>Bad</strong>, with reasons such as stop-loss placement, reward/risk and entry width. Only the signal's fields are sent, never your account or messages.</p>
<p><strong>It can be wrong.</strong> It's an early version, and its score is a confidence rating, not a promise of profit. Treat it as advice; it's being improved with each release.</p>`,
      },
      {
        id: 'speed',
        title: 'How fast is the copy?',
        html: `<p>Pipkite's own processing, from Telegram handing over the message to the order being sent to MT5, takes about <strong>${ms} ms</strong> (median, ${p95} ms at the 95th percentile) on the reference PC (${BENCH.machine}), with a crash-safe journal write before every order. Reading a signal takes about ${num(L, BENCH.parseP50Us, 1)} µs. Telegram's delivery, your broker and your network add their own time on top, and the Dashboard shows the latency measured on your own machine. Learn more on the <a href="/#speed">performance section</a>.</p>`,
      },
      {
        id: 'safety',
        title: 'Staying safe',
        html: `<ul>
<li>Pair a demo account first; move to a real account only when the results match what you expect.</li>
<li>Always keep a stop-loss rule and per-symbol spread limits. Symbols without limits don't trade.</li>
<li>Keep your PC awake (Pipkite can do this) or use a VPS: sleep and shutdown pause copying.</li>
<li>Remember no copier can guarantee fills or profits, and the AI review can be wrong.</li>
</ul>`,
      },
      {
        id: 'troubleshooting',
        title: 'Troubleshooting',
        html: `<dl>
<dt>"Telegram: Not set up"</dt><dd>Enter your API ID and hash, then sign in with your phone number.</dd>
<dt>"MT5 offline"</dt><dd>MetaTrader isn't open or the Expert Advisor isn't on a chart. Click Start MetaTrader; if MT5 is already open, close it and click Start again.</dd>
<dt>A signal shows "Unsupported"</dt><dd>That channel has no parser yet. Request one on the Signal rules screen.</dd>
<dt>A signal shows Rejected or Review</dt><dd>Open the row in History to see why. Nothing was sent.</dd>
<dt>The pairing request never appears</dt><dd>Start MetaTrader and log in to your broker first, then approve the pairing.</dd>
</dl>`,
      },
    ],
    ctaTitle: 'Ready to copy your first signal?',
    ctaText: 'Free download, free key, and a setup wizard that does the heavy lifting.',
    ctaButton: 'Download free',
  },

  changelog: {
    breadcrumb: 'Changelog',
    h1: 'Changelog',
    lead: "What's new in each Pipkite release.",
    version: (v: string) => `Version ${v}`,
    firstRelease: 'First release',
    downloadButton: (v: string) => `Download ${v}`,
    groups: [
      {
        h: 'Execution',
        items: [
          'Native C++ engine that never silently drops a signal, even under load.',
          'Entry handling: wait for the range, enter within tolerance, or pending orders only. BUY at ask, SELL at bid.',
          'Fixed-lot or risk-percent sizing that always rounds down, spread and deviation caps, split targets, trading hours and account limits.',
          'Durable journal before every order; unknown outcomes are reconciled, never blindly retried.',
          'Force-enter option for trades that slip just past the entry range.',
          'Copying is live as soon as an MT5 account is paired; Pause / Resume copying in one click (top bar or tray).',
        ],
      },
      {
        h: 'Speed',
        items: [
          `One disk flush per order instead of four: Telegram-to-MT5 overhead down from ${num(L, BENCH.ownOverheadBeforeP50Ms, 1)} ms to ${ms} ms (median).`,
          'Entry ranges react to every price quote; the EA picks up orders on ticks as well as its 10 ms timer.',
          'Keeps Windows awake while copying (optional).',
        ],
      },
      {
        h: 'Parsers and sources',
        items: [
          'Copy from many channels and groups, each with its own parser, entry policy and sizing overrides.',
          'Parsers are built by the Pipkite team: request one per channel from inside the app and it arrives as a signed update that loads without a restart.',
          'Parsers handle emojis, Markdown, odd dashes, Arabic-Indic and superscript digits, and detect commentary and edits.',
        ],
      },
      {
        h: 'AI review',
        items: [
          'Good / Uncertain / Bad opinion with reasons for every signal, shown as advice in the live feed.',
          'First version: it can be wrong and will improve over time.',
          'Only the signal fields are sent: never account details, chats or message text.',
        ],
      },
      {
        h: 'Dashboard, data and security',
        items: [
          'Live feed, account and copier profit cards, equity and P/L charts, latency percentiles.',
          'Local database with checksummed backups and optional password encryption.',
          'Signed (ECDSA P-256) modules and updates, DPAPI-protected secrets, no credential logging.',
          'Clean dark and light themes, tooltips on every button, built-in user guide.',
        ],
      },
    ],
  },

  privacy: {
    breadcrumb: 'Privacy',
    h1: 'Privacy',
    updated: 'Last updated',
    shortTitle: 'In short',
    short:
      "Your trading data, Telegram session and broker login stay on your PC. The online services receive only what they need: your name and optional email for a license key, the signal fields for an AI review, and redacted sample messages when you ask for a custom parser. We don't sell data, and this website has no ads, cookies or tracking: it only counts page views and download clicks anonymously.",
    sections: [
      {
        h: 'What stays on your PC',
        html: `<ul>
<li>Signal history, orders, settings and journals, in a local database under your Windows profile.</li>
<li>Your Telegram API ID/hash and session, encrypted with Windows DPAPI. Login codes and two-step passwords are passed to Telegram and never stored.</li>
<li>Your broker login, which stays inside MetaTrader. Pipkite never asks for it.</li>
</ul>`,
      },
      {
        h: 'License keys',
        html: '<p>When you generate a free key on this website we store your name, your email if you give one, a random request ID, the key and when it was issued. We use them to identify your key and, if you gave an email, to email you the key right away (sent from support@pipkite.com through Microsoft 365) and help you recover it.</p>',
      },
      {
        h: 'AI review',
        html: "<p>If AI review is on, the app sends the signal's symbol, side, order type, entry, stop-loss and take-profits with your license key. The key is checked, and the signal and its verdict are kept with your key to help the AI review learn and improve. Your account number, balance, broker login, chats and message text are never sent.</p>",
      },
      {
        h: 'Custom parser requests',
        html: '<p>When you click "Request a custom parser", the app sends the channel\'s ID and title, your optional note and up to 15 recent messages from that channel with links, @handles, phone numbers and emails removed. They\'re stored with your license key so the parser can be built and delivered to you.</p>',
      },
      { h: 'Updates', html: '<p>The app sends your license key when it checks for and downloads updates, so the right parsers reach you.</p>' },
      {
        h: 'This website',
        html: '<p>No ads, no cookies and no third-party analytics. To know how many people visit and download, the site counts page views (which page, which language, which day) and download clicks (installer or zip). Nothing that identifies you is sent or stored: your IP address is only used, in memory and for that day, to count a download once and to stop scripts inflating the numbers. If your browser sends Global Privacy Control or Do Not Track, nothing is counted. Your theme choice (light or dark) is saved in your own browser only. Our hosting provider may keep standard server logs.</p>',
      },
      {
        h: 'Your choices',
        html: `<ul>
<li>The app opens without a license key; parsers, the AI review and updates need one.</li>
<li>AI review can be turned off in Execution policy.</li>
<li>You can erase local history at any time in Settings.</li>
<li>To ask what we hold about your license key or to have it deleted, {contact}</li>
</ul>`,
      },
    ],
    contactEmail: (email: string) => `email <a href="mailto:${email}">${email}</a>.`,
    contactFallback: 'contact us through the app.',
    englishPrevails: '',
  },

  notFound: {
    code: '404',
    title: "This signal didn't parse",
    lead: "The page you're looking for doesn't exist or has moved.",
    home: 'Back to home',
    guide: 'Read the guide',
  },
};

export type Dict = typeof en;
