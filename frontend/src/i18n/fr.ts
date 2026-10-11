import { BENCH, DOWNLOADS, VERSION } from '../config';
import type { Dict } from './en';
import { num } from './format';

const L = 'fr' as const;
const ms = num(L, BENCH.ownOverheadP50Ms, 1);
const p95 = num(L, BENCH.ownOverheadP95Ms, 1);

export const fr: Dict = {
  meta: {
    homeTitle: 'Copieur de signaux Telegram vers MT5 gratuit | Pipkite',
    homeDesc:
      'Copiez automatiquement les signaux de trading Telegram vers MetaTrader 5 en 4 ms environ. Appli Windows gratuite, parsers faits pour vous, revue IA.',
    downloadTitle: `Télécharger Pipkite ${VERSION} – Copieur Telegram MT5`,
    downloadDesc: `Téléchargez Pipkite ${VERSION} pour Windows 10/11 : un copieur gratuit de signaux Telegram vers MetaTrader 5. Installeur ou zip portable, avec SHA-256.`,
    licenseTitle: 'Clé de licence Pipkite gratuite (sans carte, à vie)',
    licenseDesc:
      'Générez une clé de licence Pipkite gratuite en quelques secondes. Elle n’expire jamais et débloque les parsers, la revue IA et les mises à jour signées.',
    guideTitle: 'Copier les signaux Telegram vers MT5 automatiquement (2026)',
    guideDesc:
      'Guide pas à pas pour copier les signaux Telegram vers MetaTrader 5 : connecter Telegram et MT5, obtenir les parsers, régler le risque, tester sans risque.',
    changelogTitle: `Journal des versions Pipkite – Nouveautés ${VERSION}`,
    changelogDesc: `Notes de version de Pipkite, le copieur Telegram vers MT5 gratuit. Version ${VERSION} : moteur d’exécution, parsers, revue IA, vitesse, tableau de bord.`,
    privacyTitle: 'Confidentialité – Ce que Pipkite collecte et pourquoi',
    privacyDesc:
      'Ce que Pipkite garde sur votre PC, ce que reçoivent les services de licence, de revue IA et de parsers, et ce qui n’est jamais collecté. En clair.',
    notFoundTitle: 'Page introuvable | Pipkite',
    notFoundDesc: 'Cette page n’existe pas. Revenez à Pipkite, le copieur de signaux Telegram vers MT5 gratuit.',
    ogAlt: `Pipkite : signaux Telegram vers MetaTrader 5 en ${ms} ms`,
    appDescription:
      'Application Windows gratuite qui copie les signaux de trading des canaux et groupes Telegram vers MetaTrader 5. Chaque canal reçoit un parser conçu par l’équipe Pipkite ; les signaux sont vérifiés selon vos règles de risque et passés en 4 ms environ.',
    featureList: [
      'Copie automatique des signaux de canaux et groupes Telegram vers MetaTrader 5',
      `Environ ${ms} ms du message Telegram à l’ordre envoyé (médiane, mesurée)`,
      'Parsers conçus pour chaque canal par l’équipe Pipkite, livrés en mises à jour automatiques',
      'Revue IA consultative de chaque signal : verdict Bon, Mauvais ou Incertain avec explications',
      'Taille en lots fixes ou en % de risque, plafonds de spread et de slippage, horaires de trading',
      'Attend les zones d’entrée et peut forcer l’entrée si le prix les dépasse de peu',
      'Plusieurs take-profits, objectifs fractionnés',
      'Pause et reprise de la copie en un clic',
      'Journal d’ordres résistant aux pannes avec réconciliation',
      'Tableau de bord des profits et de la latence en direct',
    ],
  },

  common: {
    skip: 'Aller au contenu',
    home: 'Accueil',
    download: 'Télécharger',
    freeKey: 'Clé gratuite',
    downloadWindows: 'Télécharger pour Windows',
    getFreeKey: 'Obtenir une clé gratuite',
    getYourFreeKey: 'Obtenir votre clé gratuite',
    copy: 'Copier',
    copied: 'Copié',
    themeToggle: 'Changer de thème',
    openMenu: 'Ouvrir le menu',
    language: 'Langue',
    mainNav: 'Principal',
    mobileNav: 'Mobile',
    breadcrumb: 'Fil d’Ariane',
    onThisPage: 'Sur cette page',
    illustration: 'Illustration',
    homeAria: 'Accueil Pipkite',
  },

  nav: { how: 'Fonctionnement', features: 'Fonctionnalités', speed: 'Vitesse', ai: 'IA', guide: 'Guide', faq: 'FAQ' },

  footer: {
    tagline: 'Copieur gratuit de signaux Telegram vers MetaTrader 5 pour Windows, avec des parsers conçus pour vos canaux.',
    versionLine: (v: string) => `Version ${v} · Windows 10 / 11, 64 bits`,
    product: 'Produit',
    help: 'Aide',
    features: 'Fonctionnalités',
    performance: 'Performances',
    changelog: 'Journal des versions',
    guide: 'Copier les signaux Telegram vers MT5',
    faq: 'FAQ',
    privacy: 'Confidentialité',
    riskTitle: 'Avertissement sur les risques.',
    risk: 'Le trading de produits à effet de levier comme le forex et les CFD comporte un risque élevé de perte. Pipkite copie des signaux ; il ne peut garantir ni exécution ni profit, et la revue IA est une première version qui peut se tromper. Testez d’abord sur un compte démo.',
    legal: (year: number) =>
      `© ${year} Pipkite. MetaTrader est une marque de MetaQuotes Ltd. Telegram est une marque de Telegram FZ-LLC. Pipkite n’est affilié à aucune des deux.`,
  },

  hero: {
    badgeNew: 'Nouveau',
    badge: (v: string) => `La version ${v} est sortie. Gratuite pour Windows`,
    title: 'Les signaux Telegram dans MetaTrader 5 en',
    unit: 'ms',
    lead: 'Pipkite lit les canaux que vous choisissez, comprend chaque signal, le vérifie selon vos règles et passe l’ordre dans MT5 avant même que vous ayez déverrouillé votre téléphone.',
    leadStrong: '100 % gratuit.',
    checks: ['Gratuit à vie, sans carte', 'Parsers faits pour vous', 'Pas de droits admin', 'Windows 10 / 11'],
    shotAlt: 'Le tableau de bord Pipkite : connexions, compte et profits, flux en direct et latence',
    cardNow: 'à l’instant',
    aiReview: 'Revue IA',
    good: 'Bon',
    rr: 'gain/risque 2,2',
    orderSent: 'Ordre envoyé à MT5',
    downloadsCount: '{n} téléchargements',
  },

  how: {
    eyebrow: 'Fonctionnement',
    title: 'Du message à l’ordre sans lever le petit doigt',
    lead: 'Plus de copier-coller des prix, plus d’entrées manquées pendant votre sommeil. Voici ce qui se passe quand un signal arrive.',
    step1: '1 · Le message arrive',
    step2: '2 · Compris et vérifié',
    step3: '3 · Ordre dans MetaTrader 5',
    fields: { symbol: 'Symbole', side: 'Sens', entry: 'Entrée', entryNote: 'attente de la zone', sl: 'Stop-loss', tps: 'Take-profits' },
    aiGood: 'IA : Bon',
    checksOk: 'Spread OK · Risque OK',
    volume: 'Volume',
    sent: 'Envoyé',
    illustration: 'Illustration. La taille, les objectifs et l’entrée suivent vos propres règles.',
    setupTitle: 'Une seule configuration, avec un assistant guidé',
    steps: [
      { title: 'Connectez Telegram', text: 'Connectez-vous avec votre propre compte Telegram et cochez les canaux ou groupes à copier. Les codes de connexion ne sont jamais stockés.' },
      { title: 'Connectez MetaTrader 5', text: 'Cliquez sur « Start MetaTrader ». Pipkite installe son Expert Advisor, ouvre votre terminal et s’y associe.' },
      { title: 'Demandez vos parsers', text: 'Un clic par canal. Notre équipe crée un parser pour le format exact de ce canal ; il s’installe automatiquement, sans redémarrage.' },
      { title: 'Fixez vos règles', text: 'Lots ou % de risque, spread max, horaires, zones d’entrée. La copie démarre dès que MT5 est associé ; pause ou reprise en un clic.' },
    ],
  },

  features: {
    eyebrow: 'Fonctionnalités',
    title: 'Tout ce qu’il faut à un copieur sérieux',
    lead: 'Du C++ natif sous Windows. Pas d’extension de navigateur, pas de relais cloud entre vous et votre courtier, pas d’abonnement.',
    parserTitle: 'Comprend les signaux désordonnés',
    parserText:
      'Emojis, Markdown, chiffres arabes-indiens ou en exposant, tirets bizarres, plusieurs take-profits. Chaque canal reçoit un parser conçu par notre équipe pour son format ; il normalise le message, rattache chaque valeur au texte d’origine et refuse de deviner sur les commentaires, les modifications ou les « TP1 atteint ».',
    legend: { symbol: 'symbole', side: 'sens', label: 'libellé', ignored: 'ignoré' },
    entry: 'entrée',
    valid: 'Valide',
    rangeTitle: 'Attend votre entrée',
    rangeText: 'Pour « buy 4299–4295 », il attend que le prix entre dans la zone, en réagissant à chaque cotation. L’entrée forcée rattrape les trades qui la dépassent de peu.',
    filled: 'exécuté',
    small: [
      { title: 'Exécute selon vos règles', text: 'Lots fixes ou % de risque (toujours arrondi à l’inférieur), plafonds de spread et de slippage, horaires, limites de compte, take-profits fractionnés.' },
      { title: 'Résistant aux pannes', text: 'Chaque ordre est inscrit sur disque avant de partir. Après un plantage ou une coupure, les ordres incertains sont réconciliés, jamais renvoyés à l’aveugle.' },
      { title: 'Pause et reprise en un clic', text: 'La copie tourne dès que MT5 est associé. Un clic stoppe immédiatement les nouveaux trades et les relance aussi vite. La pause ne ferme jamais les positions ouvertes.' },
      { title: 'Des parsers faits pour vous', text: 'Rien à configurer : demandez un parser pour un canal et notre équipe le crée pour ce format précis. Il arrive en mise à jour signée et se charge sans redémarrage.' },
    ],
    dashTitle: 'Un tableau de bord qui dit la vérité',
    dashText:
      'Solde, équité, P/L latent et réalisé net de frais, un flux en direct de chaque signal et de son sort, et les percentiles de latence mesurés sur votre propre PC. Les données du courtier font foi.',
    equity: 'Équité',
    floating: 'P/L latent',
    latency: 'Latence p95',
  },

  speed: {
    eyebrow: 'Vitesse',
    title: 'Mesuré, pas promis.',
    leadHtml: `En copy trading, chaque milliseconde a un prix. Tout le trajet de Pipkite, du message Telegram jusqu’à l’ordre envoyé à votre courtier, tourne en code natif avec une seule écriture durable, et prend environ <strong>${ms} ms</strong>.`,
    proof: 'Inutile de nous croire sur parole : le tableau de bord affiche en direct les percentiles de latence mesurés sur votre PC.',
    stats: [
      { label: 'Telegram → envoi MT5', note: `temps propre à Pipkite, médiane (p95 ${p95} ms)` },
      { label: 'Pour lire un signal', note: `temps d’analyse médian, p95 ${num(L, BENCH.parseP95Us, 1)} µs` },
      { label: 'Signaux par seconde', note: 'débit du parser hors ligne' },
      { label: 'Écriture disque par ordre', note: `contre ${BENCH.flushesBefore} avant, toujours sans perte` },
    ],
    chartTitle: 'Message Telegram → ordre envoyé à MT5',
    chartSub: 'Médiane, en millisecondes. Plus court = mieux.',
    fastest: 'Le plus rapide testé',
    rowUs: 'Pipkite 1.0',
    rowBefore: 'Avant optimisation',
    improvementStrong: '3,6× plus rapide que notre première version.',
    improvement: 'Une écriture disque par ordre au lieu de quatre, les écritures critiques passent en priorité, et les zones d’entrée réagissent à chaque cotation au lieu d’un minuteur de 100 ms.',
    reference: `Machine de référence : ${BENCH.machine}, build Release, journal durable activé. La livraison Telegram, votre courtier et votre réseau ajoutent leur propre délai.`,
  },

  ai: {
    exampleLabel: 'Exemple de revue IA',
    reasons: ['Le stop-loss est du bon côté de l’entrée', 'Gain/risque jusqu’au dernier objectif : 2,2 : 1', 'Le premier objectif est plus proche que le stop-loss'],
    good: 'Bon',
    eyebrow: 'IA incluse',
    title: 'Un second avis sur chaque signal, gratuitement',
    lead: 'Le backend IA de Pipkite tourne déjà. Rien à installer, aucune clé d’API à acheter : ajoutez votre clé de licence gratuite et chaque signal reçoit un second avis.',
    points: [
      { title: 'Des verdicts clairs et expliqués', text: 'Bon, Mauvais ou Incertain, avec les raisons : placement du stop, gain/risque et largeur de la zone d’entrée.' },
      { title: 'Un conseil, pas un ordre', text: 'Le verdict s’affiche à côté de chaque signal dans le flux en direct pour vous aider à juger un fournisseur. Activez-le ou non dans Execution policy.' },
      { title: 'Seul le signal est envoyé', text: 'Symbole, sens, entrée, SL et TP. Jamais votre compte, votre solde, vos identifiants courtier, vos discussions ni le texte des messages. Rien n’est conservé une fois la réponse donnée.' },
    ],
    warnStrong: 'Encore jeune : l’IA peut se tromper.',
    warn: 'C’est une première version : elle peut rater des choses, et son score est un indice de confiance, pas une promesse de gain. Nous l’améliorons à chaque version ; prenez-la comme un second avis, jamais comme une garantie.',
  },

  screens: {
    eyebrow: 'Aperçu',
    title: 'Clair, calme et facile à lire',
    lead: 'Thèmes sombre et clair (changez le thème de ce site pour voir les deux), une info-bulle sur chaque bouton et un guide complet intégré.',
    tablist: 'Écrans de l’application',
    screenAlt: (label: string, text: string) => `Écran ${label} de Pipkite. ${text}`,
    items: {
      dashboard: { label: 'Tableau de bord', text: 'Connexions, compte et profits, flux en direct et latence, sur un seul écran.' },
      history: { label: 'Historique', text: 'Chaque message, comment il a été lu et ce qui a été fait, avec la raison. Export CSV ou JSON.' },
      metatrader: { label: 'MetaTrader', text: 'Lancez MT5 avec l’EA, validez l’association et choisissez le symbole de chaque canal.' },
      'execution-policy': { label: 'Règles d’exécution', text: 'Taille, take-profits, timing et limites. Chaque option a un « ? » qui l’explique.' },
      'signal-rules': { label: 'Parsers', text: 'Voyez quel parser lit chaque canal et demandez-en un sur mesure en un clic.' },
    },
    note: 'L’interface de l’application est en anglais.',
  },

  free: {
    eyebrow: 'Tarif',
    title: 'Gratuit. Ni essai, ni freemium.',
    lead: 'Pas d’abonnement, pas de carte, pas d’offre « pro » qui cache le meilleur. Téléchargez, obtenez votre clé gratuite et profitez de tout.',
    price: '0 €',
    forever: 'pour toujours',
    included: [
      'L’application complète, sans limite',
      'Canaux et signaux Telegram illimités',
      'Des parsers faits pour vos canaux',
      'Revue IA de chaque signal',
      'Mises à jour automatiques signées',
      'Tableau de bord des profits et de la latence',
    ],
    privacyEyebrow: 'Confidentialité et sécurité',
    privacyTitle: 'Gratuit ne veut pas dire que vous êtes le produit',
    privacy: [
      { title: 'Vos données restent sur votre PC', text: 'Historique, réglages et journaux sont dans une base locale. Les secrets sont chiffrés avec Windows DPAPI.' },
      { title: 'Jamais vos mots de passe courtier', text: 'Vos identifiants courtier restent dans MetaTrader. Pipkite ne les demande jamais.' },
      { title: 'Modules et mises à jour signés', text: 'Chaque module de parser et chaque version sont signés ECDSA et vérifiés avant chargement.' },
    ],
  },

  faq: {
    eyebrow: 'FAQ',
    title: 'Vos questions, nos réponses',
    leadHtml:
      'Vous débutez ? Lisez le <a href="/telegram-to-mt5-copier">guide pas à pas</a>. L’application inclut aussi un guide complet avec des captures de chaque écran.',
    items: [
      {
        q: 'Qu’est-ce que Pipkite ?',
        a: `Pipkite est une application Windows gratuite qui copie les signaux de trading des canaux et groupes Telegram vers MetaTrader 5. Elle lit chaque nouveau message avec un parser conçu pour ce canal, extrait le symbole, le sens, l’entrée, le stop-loss et les take-profits, les vérifie selon vos règles de risque et passe l’ordre dans votre propre terminal MT5 en ${ms} ms environ. Une revue IA facultative donne un second avis sur chaque signal.`,
      },
      {
        q: 'Pipkite est-il vraiment gratuit ?',
        a: 'Oui. L’application, les parsers, la revue IA et les mises à jour sont gratuits, sans abonnement, sans essai et sans carte. La clé de licence est gratuite aussi ; elle identifie votre installation auprès des services de parsers, d’IA et de mise à jour.',
      },
      {
        q: 'Comment copier automatiquement les signaux Telegram vers MT5 ?',
        a: 'Installez Pipkite, connectez-vous avec votre compte Telegram et cochez les canaux à copier, cliquez sur « Start MetaTrader » pour attacher l’Expert Advisor de Pipkite et validez l’association, demandez un parser pour chaque canal et réglez votre taille de lot ou votre % de risque. Dès que le parser d’un canal est installé, ses nouveaux signaux sont passés dans MT5 tout seuls. Mettez en pause ou reprenez la copie en un clic.',
      },
      {
        q: 'Quelle est sa vitesse, honnêtement ?',
        a: `La part propre à Pipkite, entre le moment où Telegram transmet le message et l’envoi de l’ordre à MT5, prend environ ${ms} ms (médiane) sur le PC de référence. La livraison Telegram, votre courtier et votre connexion ajoutent leur propre délai. Le tableau de bord affiche la latence mesurée sur votre machine.`,
      },
      {
        q: 'Est-ce compatible avec mon courtier ?',
        a: 'Pipkite passe les ordres via un Expert Advisor dans votre propre terminal MetaTrader 5 de bureau sous Windows : il fonctionne donc avec les courtiers sur lesquels vous tradez dans MT5. Vos identifiants restent dans MetaTrader. Associez chaque symbole de canal (par exemple GOLD) au nom utilisé par votre courtier (par exemple XAUUSD).',
      },
      { q: 'Est-ce compatible avec MetaTrader 4 ?', a: 'Non. Pipkite prend en charge MetaTrader 5 uniquement.' },
      {
        q: 'Peut-il copier depuis des canaux et groupes privés ?',
        a: 'Oui. Pipkite se connecte à Telegram en votre nom : il peut lire tout canal ou groupe dont votre compte est membre, y compris privé. Seuls les messages reçus après la connexion sont copiés ; l’historique ne devient jamais des ordres.',
      },
      {
        q: 'Puis-je l’essayer sans risquer d’argent ?',
        a: 'Oui. Connectez d’abord un compte MT5 démo : la copie fonctionne exactement pareil, sans rien en jeu. L’assistant propose aussi une étape « Test a sample » qui montre ce que Pipkite lit dans un message collé, sans rien envoyer. La copie est active dès que MT5 est associé, et la pause stoppe aussitôt les nouveaux trades.',
      },
      {
        q: 'Dois-je configurer ou écrire un parser ?',
        a: 'Non, et vous ne pouvez pas : les parsers sont créés par l’équipe Pipkite. Cliquez sur « Request a custom parser » dans l’écran Signal rules ; quelques messages récents du canal sont envoyés sans liens, @pseudos, numéros ni e-mails, nous créons un parser pour son format exact et il s’installe automatiquement en mise à jour signée, sans redémarrage. Tant que le parser d’un canal n’est pas arrivé, ses messages ne sont pas tradés.',
      },
      {
        q: 'La revue IA a-t-elle toujours raison ?',
        a: 'Non. La revue IA est une première version et elle peut se tromper. Elle évalue la construction de chaque signal (placement du stop, gain/risque, largeur d’entrée) et donne un score de confiance, qui n’est pas une promesse de gain. Elle s’affiche comme un conseil à côté de chaque signal et nous l’améliorons au fil du temps.',
      },
      {
        q: 'L’IA voit-elle mon compte ?',
        a: 'Non. Seuls les champs du signal sont envoyés : symbole, sens, type d’ordre, entrée, stop-loss et take-profits. Jamais votre numéro de compte, votre solde, vos identifiants courtier, vos discussions ni le texte des messages. Rien du signal n’est conservé après la réponse.',
      },
      {
        q: 'Quelles langues sont prises en charge ?',
        a: 'Ce site est disponible en anglais, français, espagnol et arabe. L’application Pipkite elle-même est pour l’instant en anglais. Les parsers fonctionnent avec des signaux écrits dans n’importe quelle langue, puisque chacun est créé pour son canal.',
      },
      {
        q: 'Mon PC doit-il rester allumé ?',
        a: 'Oui. Pipkite et MetaTrader tournent sur votre PC Windows : la veille, l’arrêt ou un terminal MT5 fermé suspendent la copie. Pipkite peut garder Windows éveillé pendant la copie ; pour copier 24 h/24, utilisez un VPS Windows.',
      },
      {
        q: 'Windows affiche un avertissement SmartScreen. Est-ce normal ?',
        a: 'Cette première version n’est pas encore signée avec un certificat payant, donc Windows peut vous avertir. Choisissez « Informations complémentaires → Exécuter quand même ». Vous pouvez vérifier le fichier avec la somme SHA-256 de la page de téléchargement.',
      },
    ],
  },

  cta: {
    title: 'Ne ratez plus d’entrées. Copiez en quelques millisecondes.',
    lead: 'Téléchargement gratuit, clé gratuite et assistant de configuration guidé.',
  },

  download: {
    breadcrumb: 'Télécharger',
    h1: 'Téléchargez le copieur Telegram vers MT5 gratuit',
    sub: (v: string) => `Pipkite ${v} pour Windows 10 et 11. Sortie le`,
    whatsNew: 'Nouveautés',
    installer: { title: 'Installeur', badge: 'Recommandé', text: 'S’installe pour vous seul, sans droits administrateur. Ajoute un raccourci au menu Démarrer et le guide utilisateur.' },
    portable: { title: 'Zip portable', text: 'Décompressez où vous pouvez écrire et lancez Pipkite.Launcher.exe. Rien n’est installé.' },
    sha: 'SHA-256',
    checkHint: 'Vérifier un téléchargement dans PowerShell :',
    stepsTitle: 'Prêt en trois étapes',
    steps: [
      { title: 'Lancez l’installeur', text: `Ouvrez ${DOWNLOADS.installer.name}. Si SmartScreen apparaît, choisissez « Informations complémentaires → Exécuter quand même ».` },
      { title: 'Ajoutez votre clé gratuite', text: 'L’assistant d’accueil demande votre clé de licence. Collez-la et cliquez sur « Validate & save ».' },
      { title: 'Suivez l’assistant', text: 'Connectez Telegram, lancez MetaTrader, choisissez vos canaux, demandez leurs parsers et testez un signal.' },
    ],
    noKey: 'Pas encore de clé ? Obtenez-en une gratuitement',
    reqTitle: 'Configuration requise',
    req: [
      'Windows 11 ou Windows 10 22H2, 64 bits',
      '2 cœurs ou plus, 4 Go de RAM, SSD recommandé',
      'MetaTrader 5, connecté à votre courtier',
      'Un compte Telegram',
      '.NET Framework 4.8 (inclus dans Windows 10/11)',
    ],
    reqNote: 'La veille, l’arrêt ou un terminal MT5 fermé suspendent la copie. Pour copier 24 h/24, utilisez un VPS Windows. L’interface de l’application est en anglais.',
  },

  license: {
    breadcrumb: 'Clé de licence gratuite',
    h1: 'Votre clé, gratuite et à vie',
    lead: 'Aucun paiement, aucune carte. La clé relie votre installation aux services en ligne de Pipkite :',
    unlocks: ['Des parsers faits pour vos canaux', 'Revue IA de chaque signal', 'Mises à jour automatiques signées'],
    skipHtml: 'L’application s’ouvre sans clé, mais les parsers, la revue IA et les mises à jour en ont besoin. Ajoutez-la dans l’assistant d’accueil ou plus tard dans <strong>Settings → AI and licensing</strong>.',
    name: 'Votre nom',
    namePh: 'Jeanne Trader',
    email: 'E-mail',
    emailOpt: '(facultatif, pour récupérer votre clé)',
    emailPh: 'vous@exemple.com',
    submit: 'Générer ma clé gratuite',
    generating: 'Génération…',
    detailsUse: 'Vos informations servent uniquement à identifier votre clé.',
    privacy: 'Confidentialité',
    ready: 'Votre clé de licence est prête',
    copyKey: 'Copier la clé',
    saveIt: 'Conservez-la en lieu sûr. Elle n’est affichée qu’une fois.',
    afterHtml: [
      'Ouvrez Pipkite (ou <a href="/download">téléchargez-le</a>).',
      'Collez la clé dans l’assistant d’accueil, ou dans Settings → AI and licensing.',
      'Cliquez sur <strong>Validate &amp; save</strong>. Le statut passe au vert.',
    ],
    errors: {
      name: 'Veuillez saisir votre nom.',
      email: 'Cette adresse e-mail ne semble pas correcte.',
      bad: 'Vérifiez vos informations et réessayez.',
      rate: 'Trop de demandes. Patientez une minute et réessayez.',
      server: 'Le service de licence rencontre un problème. Réessayez dans un instant.',
      unexpected: 'Le service de licence a renvoyé une réponse inattendue.',
      network: 'Impossible de joindre le service de licence. Vérifiez votre connexion et réessayez.',
    },
  },

  guide: {
    breadcrumb: 'Guide Telegram vers MT5',
    h1: 'Comment copier automatiquement les signaux Telegram vers MetaTrader 5',
    lead: 'Un guide complet et pratique : d’un signal publié dans un canal Telegram à un ordre dans votre terminal MT5, sans recopier les prix à la main.',
    updated: 'Mis à jour le',
    by: 'Par l’équipe Pipkite',
    readTime: '7 min de lecture',
    shortTitle: 'En bref',
    short: `Pour copier automatiquement les signaux Telegram vers MT5, installez un copieur Telegram vers MT5 comme Pipkite, connectez-vous avec votre compte Telegram, choisissez les canaux à copier, attachez l’Expert Advisor du copieur à votre terminal MetaTrader 5, demandez un parser pour chaque canal et réglez votre taille de lot ou votre % de risque. Chaque nouveau signal est alors analysé et passé en ordre tout seul, en ${ms} ms environ. Un clic met la copie en pause ou la relance.`,
    figures: {
      metatrader: { alt: 'Écran MetaTrader de Pipkite avec la liste des terminaux, le bouton Start MetaTrader et l’état de l’association', caption: 'L’écran MetaTrader : lancer le terminal, valider l’association, associer les symboles.' },
      'signal-rules': { alt: 'Écran Signal rules de Pipkite listant les canaux, l’état de leur parser et le bouton Request a custom parser', caption: 'Signal rules : le parser de chaque canal et le bouton pour en demander un.' },
      'execution-policy': { alt: 'Écran Execution policy de Pipkite avec la taille de position, les take-profits et le timing', caption: 'Execution policy : quelle taille, quels objectifs et quand ne pas trader.' },
    },
    sections: [
      {
        id: 'what-is',
        title: 'Qu’est-ce qu’un copieur Telegram vers MT5 ?',
        html: `<p>Un copieur Telegram vers MT5 est un logiciel qui surveille des canaux ou groupes Telegram et transforme chaque signal de trading en ordre dans MetaTrader 5. Un signal comme « GOLD BUY 4299–4295, SL 4282, TP 4307 » est lu, le symbole, le sens, l’entrée, le stop-loss et les take-profits sont extraits, vos propres règles fixent la taille, et l’ordre part chez votre courtier via MT5.</p>
<p>Copier à la main est lent et source d’erreurs : le temps de déverrouiller votre téléphone, de lire le message et de saisir l’ordre, le prix a souvent bougé. Un copieur supprime ce délai et les fautes de frappe, et continue de travailler pendant que vous dormez.</p>`,
      },
      {
        id: 'need',
        title: 'Ce qu’il vous faut',
        html: `<ul>
<li>Un PC Windows 10 (22H2) ou Windows 11, 64 bits, avec 4 Go de RAM. Un VPS Windows convient pour copier 24 h/24.</li>
<li>MetaTrader 5 (version bureau) connecté à votre courtier. Un compte démo est idéal pour le premier essai.</li>
<li>Votre compte Telegram, membre des canaux ou groupes que vous voulez copier.</li>
<li>Un API ID et un hash Telegram gratuits sur <code>my.telegram.org</code> (une minute).</li>
<li>Pipkite et une <a href="/license">clé de licence gratuite</a>, nécessaire pour demander des parsers, les recevoir en mise à jour (et pour la revue IA).</li>
</ul>`,
      },
      {
        id: 'step-1',
        title: 'Étape 1 : installer Pipkite',
        html: `<p><a href="/download">Téléchargez l’installeur</a> et lancez-le. Il s’installe pour votre seul utilisateur Windows, sans droits administrateur. Si Windows SmartScreen apparaît, choisissez <strong>Informations complémentaires → Exécuter quand même</strong> ; vous pouvez vérifier le fichier avec la somme SHA-256 de la page de téléchargement. Au premier lancement, un court assistant vous guide à travers chaque étape ci-dessous, avec un statut en direct pour chacune. L’interface de l’application est en anglais.</p>`,
      },
      {
        id: 'step-2',
        title: 'Étape 2 : connecter Telegram',
        html: `<ol>
<li>Allez sur <code>my.telegram.org</code>, ouvrez <strong>API development tools</strong> et copiez l’API ID et l’API hash.</li>
<li>Collez-les dans Pipkite et cliquez sur <strong>Save credentials</strong>. Ils sont chiffrés sur votre PC avec Windows DPAPI et jamais journalisés.</li>
<li>Saisissez votre numéro, cliquez sur <strong>Send code</strong> et tapez le code reçu de Telegram (et votre mot de passe en deux étapes si vous en avez un).</li>
<li>Cochez les canaux et groupes à copier et cliquez sur <strong>Add checked as sources</strong>. Les canaux privés fonctionnent aussi, tant que votre compte en est membre.</li>
</ol>
<p>Seuls les messages reçus après la connexion sont copiés. L’historique ne devient jamais des ordres.</p>`,
      },
      {
        id: 'step-3',
        title: 'Étape 3 : connecter MetaTrader 5',
        figure: 'metatrader',
        html: `<ol>
<li>Dans l’écran <strong>MetaTrader</strong>, choisissez votre terminal et cliquez sur <strong>Start MetaTrader</strong>. Pipkite installe son Expert Advisor, ouvre MT5 avec l’EA sur un graphique et active les réglages nécessaires. Il demande d’abord.</li>
<li>Connectez-vous à votre courtier dans MetaTrader comme d’habitude. Pipkite ne demande jamais votre mot de passe courtier.</li>
<li>De retour dans Pipkite, cliquez sur <strong>Approve pairing</strong> et tapez votre numéro de compte pour confirmer. Associez d’abord un <strong>compte démo</strong> : dès qu’un compte est associé, la copie est active.</li>
<li>Associez le symbole de chaque canal au nom utilisé par votre courtier, par exemple « GOLD » à « XAUUSD ».</li>
</ol>`,
      },
      {
        id: 'step-4',
        title: 'Étape 4 : demander un parser pour chaque canal',
        figure: 'signal-rules',
        html: `<p>Chaque fournisseur écrit ses signaux à sa façon : chaque canal a donc besoin d’un <em>parser</em> conçu pour son format. Pipkite n’en contient aucun au départ, et vous ne les créez ni ne les modifiez vous-même : c’est l’équipe Pipkite qui s’en charge.</p>
<ol>
<li>Ouvrez <strong>Signal rules</strong>, sélectionnez un canal et cliquez sur <strong>Request a custom parser for this channel</strong>. Ajoutez une note si vous le souhaitez.</li>
<li>Pipkite envoie quelques messages récents de ce canal, sans liens, @pseudos, numéros de téléphone ni e-mails.</li>
<li>Nous créons le parser et le publions en mise à jour signée. Il s’installe automatiquement et fonctionne sans redémarrage ; <strong>Check for parser updates</strong> le récupère immédiatement.</li>
</ol>
<p>Tant que le parser d’un canal n’est pas arrivé, ses messages sont marqués non pris en charge et ne sont jamais tradés.</p>`,
      },
      {
        id: 'step-5',
        title: 'Étape 5 : fixer vos règles de risque',
        figure: 'execution-policy',
        html: `<ul>
<li><strong>Taille de position :</strong> un lot fixe, ou un pourcentage de l’équité risqué au stop-loss. Le calcul au risque arrondit toujours au pas de lot inférieur ; si même le lot minimum est trop gros, le trade est ignoré plutôt que forcé.</li>
<li><strong>Take-profits :</strong> un seul objectif, ou une position répartie sur plusieurs.</li>
<li><strong>Limites :</strong> âge maximal du signal, spread et slippage maximums par symbole, positions ouvertes, perte journalière et horaires de trading.</li>
<li><strong>Zones d’entrée :</strong> pour les signaux « buy 4299–4295 », Pipkite attend que le prix entre dans la zone. L’option <em>force-enter</em> prend le trade si le prix l’a dépassée de quelques pips que vous choisissez.</li>
<li><strong>Revue IA :</strong> activez « review every signal » pour voir un avis Bon / Incertain / Mauvais à côté de chaque signal dans le flux en direct.</li>
</ul>`,
      },
      {
        id: 'step-6',
        title: 'Étape 6 : tester, puis laisser copier',
        html: `<p>Il n’y a pas de mode aperçu séparé : dès que MT5 est associé et qu’un canal a son parser, les signaux valides sont copiés. Testez donc avant d’engager de l’argent :</p>
<ul>
<li>Dans l’étape <strong>Test a sample</strong> de l’assistant, collez un vrai message du canal pour voir exactement ce que Pipkite en lit. Rien n’est envoyé.</li>
<li>Commencez sur un <strong>compte démo</strong> et observez quelques vrais signaux dans le tableau de bord et l’historique. Chaque ligne explique sa décision : un signal rejeté vous dit pourquoi (pas de stop-loss, symbole non associé, spread trop large, etc.).</li>
<li>Utilisez <strong>Pause copying</strong> dans la barre du haut (ou l’icône de la barre des tâches) pour stopper immédiatement les nouveaux trades, et <strong>Resume copying</strong> pour reprendre. La pause ne ferme jamais les positions ouvertes.</li>
</ul>`,
      },
      {
        id: 'formats',
        title: 'Quels formats de signaux fonctionnent ?',
        html: `<p>Pratiquement tout format qu’un fournisseur utilise de façon régulière, dans n’importe quelle langue, puisque le parser de chaque canal est conçu pour son style. Les parsers gèrent les emojis, le Markdown, les tirets inhabituels, les chiffres arabes-indiens ou en exposant et les take-profits multiples, et ignorent les commentaires comme « TP1 atteint » au lieu de deviner. Si un fournisseur change sa façon de publier, redemandez le parser et la version à jour arrive automatiquement.</p>`,
      },
      {
        id: 'ai',
        title: 'À quoi sert la revue IA ?',
        html: `<p>La revue IA donne un second avis sur chaque signal : <strong>Bon</strong>, <strong>Incertain</strong> ou <strong>Mauvais</strong>, avec des raisons comme le placement du stop-loss, le gain/risque et la largeur de la zone d’entrée. Seuls les champs du signal sont envoyés, jamais votre compte ni vos messages.</p>
<p><strong>Elle peut se tromper.</strong> C’est une première version, et son score est un indice de confiance, pas une promesse de gain. Prenez-la comme un conseil ; elle s’améliore à chaque version.</p>`,
      },
      {
        id: 'speed',
        title: 'Quelle est la vitesse de copie ?',
        html: `<p>Le traitement propre à Pipkite, entre le moment où Telegram transmet le message et l’envoi de l’ordre à MT5, prend environ <strong>${ms} ms</strong> (médiane, ${p95} ms au 95e centile) sur le PC de référence (${BENCH.machine}), avec une écriture de journal sans perte avant chaque ordre. Lire un signal prend environ ${num(L, BENCH.parseP50Us, 1)} µs. La livraison Telegram, votre courtier et votre réseau ajoutent leur propre délai, et le tableau de bord affiche la latence mesurée sur votre machine. Plus de détails dans la <a href="/#speed">section performances</a>.</p>`,
      },
      {
        id: 'safety',
        title: 'Rester prudent',
        html: `<ul>
<li>Associez d’abord un compte démo ; passez sur un compte réel seulement quand les résultats correspondent à vos attentes.</li>
<li>Gardez toujours une règle de stop-loss et des limites de spread par symbole. Les symboles sans limites ne sont pas tradés.</li>
<li>Gardez votre PC éveillé (Pipkite peut s’en charger) ou utilisez un VPS : la veille et l’arrêt suspendent la copie.</li>
<li>Aucun copieur ne peut garantir exécution ni profit, et la revue IA peut se tromper.</li>
</ul>`,
      },
      {
        id: 'troubleshooting',
        title: 'Dépannage',
        html: `<dl>
<dt>« Telegram: Not set up »</dt><dd>Saisissez votre API ID et votre hash, puis connectez-vous avec votre numéro.</dd>
<dt>« MT5 offline »</dt><dd>MetaTrader n’est pas ouvert ou l’Expert Advisor n’est pas sur un graphique. Cliquez sur Start MetaTrader ; si MT5 est déjà ouvert, fermez-le et cliquez à nouveau sur Start.</dd>
<dt>Un signal indique « Unsupported »</dt><dd>Ce canal n’a pas encore de parser. Demandez-en un dans l’écran Signal rules.</dd>
<dt>Un signal indique Rejected ou Review</dt><dd>Ouvrez la ligne dans l’historique pour voir pourquoi. Rien n’a été envoyé.</dd>
<dt>La demande d’association n’apparaît jamais</dt><dd>Lancez MetaTrader et connectez-vous d’abord à votre courtier, puis validez l’association.</dd>
</dl>`,
      },
    ],
    ctaTitle: 'Prêt à copier votre premier signal ?',
    ctaText: 'Téléchargement gratuit, clé gratuite et un assistant qui fait le gros du travail.',
    ctaButton: 'Télécharger gratuitement',
  },

  changelog: {
    breadcrumb: 'Journal des versions',
    h1: 'Journal des versions',
    lead: 'Les nouveautés de chaque version de Pipkite.',
    version: (v: string) => `Version ${v}`,
    firstRelease: 'Première version',
    downloadButton: (v: string) => `Télécharger la ${v}`,
    groups: [
      {
        h: 'Exécution',
        items: [
          'Moteur C++ natif qui ne perd jamais un signal en silence, même sous charge.',
          'Gestion de l’entrée : attendre la zone, entrer dans une tolérance, ou ordres en attente uniquement. BUY au ask, SELL au bid.',
          'Taille en lots fixes ou en % de risque toujours arrondie à l’inférieur, plafonds de spread et de déviation, objectifs fractionnés, horaires et limites de compte.',
          'Journal durable avant chaque ordre ; les résultats incertains sont réconciliés, jamais renvoyés à l’aveugle.',
          'Option d’entrée forcée pour les trades qui dépassent de peu la zone d’entrée.',
          'La copie est active dès qu’un compte MT5 est associé ; pause / reprise en un clic (barre du haut ou barre des tâches).',
        ],
      },
      {
        h: 'Vitesse',
        items: [
          `Une écriture disque par ordre au lieu de quatre : délai Telegram vers MT5 réduit de ${num(L, BENCH.ownOverheadBeforeP50Ms, 1)} ms à ${ms} ms (médiane).`,
          'Les zones d’entrée réagissent à chaque cotation ; l’EA récupère les ordres à chaque tick ainsi qu’avec son minuteur de 10 ms.',
          'Garde Windows éveillé pendant la copie (facultatif).',
        ],
      },
      {
        h: 'Parsers et sources',
        items: [
          'Copie depuis de nombreux canaux et groupes, chacun avec son parser, sa politique d’entrée et ses réglages de taille.',
          'Les parsers sont créés par l’équipe Pipkite : demandez-en un par canal depuis l’application, il arrive en mise à jour signée qui se charge sans redémarrage.',
          'Les parsers gèrent emojis, Markdown, tirets inhabituels, chiffres arabes-indiens et en exposant, et détectent commentaires et modifications.',
        ],
      },
      {
        h: 'Revue IA',
        items: [
          'Avis Bon / Incertain / Mauvais avec raisons pour chaque signal, affiché comme un conseil dans le flux en direct.',
          'Première version : elle peut se tromper et s’améliorera avec le temps.',
          'Seuls les champs du signal sont envoyés : jamais les détails du compte, les discussions ni le texte des messages.',
        ],
      },
      {
        h: 'Tableau de bord, données et sécurité',
        items: [
          'Flux en direct, cartes de compte et de profits du copieur, graphiques d’équité et de P/L, percentiles de latence.',
          'Base locale avec sauvegardes vérifiées et chiffrement facultatif par mot de passe.',
          'Modules et mises à jour signés (ECDSA P-256), secrets protégés par DPAPI, aucun identifiant journalisé.',
          'Thèmes sombre et clair soignés, info-bulles sur chaque bouton, guide utilisateur intégré.',
        ],
      },
    ],
  },

  privacy: {
    breadcrumb: 'Confidentialité',
    h1: 'Confidentialité',
    updated: 'Dernière mise à jour',
    shortTitle: 'En bref',
    short:
      'Vos données de trading, votre session Telegram et vos identifiants courtier restent sur votre PC. Les services en ligne ne reçoivent que le nécessaire : votre nom et un e-mail facultatif pour la clé de licence, les champs du signal pour la revue IA, et des messages d’exemple expurgés quand vous demandez un parser. Nous ne vendons aucune donnée, et ce site n’a ni publicité, ni cookies, ni pistage : il compte seulement, de façon anonyme, les pages vues et les clics de téléchargement.',
    sections: [
      {
        h: 'Ce qui reste sur votre PC',
        html: `<ul>
<li>L’historique des signaux, les ordres, les réglages et les journaux, dans une base locale de votre profil Windows.</li>
<li>Votre API ID/hash et votre session Telegram, chiffrés avec Windows DPAPI. Les codes de connexion et mots de passe en deux étapes sont transmis à Telegram et jamais stockés.</li>
<li>Vos identifiants courtier, qui restent dans MetaTrader. Pipkite ne les demande jamais.</li>
</ul>`,
      },
      {
        h: 'Clés de licence',
        html: '<p>Quand vous générez une clé gratuite sur ce site, nous enregistrons votre nom, votre e-mail si vous le donnez, un identifiant de demande aléatoire, la clé et sa date d’émission. Ils servent à identifier votre clé et, si vous avez donné un e-mail, à vous aider à la récupérer.</p>',
      },
      {
        h: 'Revue IA',
        html: '<p>Si la revue IA est activée, l’application envoie le symbole, le sens, le type d’ordre, l’entrée, le stop-loss et les take-profits du signal avec votre clé de licence. La clé est vérifiée, le signal est analysé en mémoire, et rien n’en est conservé. Votre numéro de compte, votre solde, vos identifiants courtier, vos discussions et le texte des messages ne sont jamais envoyés.</p>',
      },
      {
        h: 'Demandes de parser',
        html: '<p>Quand vous cliquez sur « Request a custom parser », l’application envoie l’identifiant et le titre du canal, votre note facultative et jusqu’à 15 messages récents de ce canal, sans liens, @pseudos, numéros de téléphone ni e-mails. Ils sont stockés avec votre clé de licence pour créer le parser et vous le livrer.</p>',
      },
      { h: 'Mises à jour', html: '<p>L’application envoie votre clé de licence lorsqu’elle vérifie et télécharge les mises à jour, pour que les bons parsers vous parviennent.</p>' },
      {
        h: 'Ce site',
        html: '<p>Ni publicité, ni cookies, ni outil d’analyse tiers. Pour savoir combien de personnes visitent et téléchargent, le site compte les pages vues (quelle page, quelle langue, quel jour) et les clics de téléchargement (installeur ou zip). Rien qui vous identifie n’est envoyé ni stocké : votre adresse IP sert uniquement, en mémoire et pour la journée, à compter un téléchargement une seule fois et à empêcher des scripts de gonfler les chiffres. Si votre navigateur envoie Global Privacy Control ou Do Not Track, rien n’est compté. Votre choix de thème (clair ou sombre) est enregistré uniquement dans votre navigateur. Notre hébergeur peut conserver des journaux serveur standard.</p>',
      },
      {
        h: 'Vos choix',
        html: `<ul>
<li>L’application s’ouvre sans clé de licence ; les parsers, la revue IA et les mises à jour en ont besoin.</li>
<li>La revue IA peut être désactivée dans Execution policy.</li>
<li>Vous pouvez effacer l’historique local à tout moment dans Settings.</li>
<li>Pour savoir ce que nous détenons sur votre clé de licence ou demander sa suppression, {contact}</li>
</ul>`,
      },
    ],
    contactEmail: (email: string) => `écrivez à <a href="mailto:${email}">${email}</a>.`,
    contactFallback: 'contactez-nous depuis l’application.',
    englishPrevails: 'Cette page est une traduction ; en cas de divergence, la version anglaise fait foi.',
  },

  notFound: {
    code: '404',
    title: 'Ce signal n’a pas pu être lu',
    lead: 'La page que vous cherchez n’existe pas ou a été déplacée.',
    home: 'Retour à l’accueil',
    guide: 'Lire le guide',
  },
};
