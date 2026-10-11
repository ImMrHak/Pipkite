import { BENCH, DOWNLOADS, VERSION } from '../config';
import type { Dict } from './en';
import { num } from './format';

const L = 'es' as const;
const ms = num(L, BENCH.ownOverheadP50Ms, 1);
const p95 = num(L, BENCH.ownOverheadP95Ms, 1);

export const es: Dict = {
  meta: {
    homeTitle: 'Copiador gratis de señales de Telegram a MT5 | Pipkite',
    homeDesc:
      'Copia automáticamente señales de trading de Telegram a MetaTrader 5 en unos 4 ms. App gratis para Windows, parsers hechos para ti y revisión con IA.',
    downloadTitle: `Descargar Pipkite ${VERSION} – Copiador Telegram MT5 gratis`,
    downloadDesc: `Descarga Pipkite ${VERSION} para Windows 10/11: un copiador gratis de señales de Telegram a MetaTrader 5. Instalador o zip portátil, con SHA-256.`,
    licenseTitle: 'Licencia gratis de Pipkite (sin tarjeta, no caduca)',
    licenseDesc:
      'Genera en segundos una clave de licencia gratis de Pipkite. Nunca caduca y desbloquea los parsers, la revisión con IA y las actualizaciones firmadas.',
    guideTitle: 'Cómo copiar señales de Telegram a MT5 automáticamente (2026)',
    guideDesc:
      'Guía paso a paso para copiar señales de Telegram a MetaTrader 5: conecta Telegram y MT5, consigue los parsers, fija tus reglas de riesgo y prueba seguro.',
    changelogTitle: `Novedades de Pipkite – Versión ${VERSION}`,
    changelogDesc: `Notas de versión de Pipkite, el copiador gratis de Telegram a MT5. Versión ${VERSION}: motor de ejecución, parsers, revisión con IA, velocidad y panel.`,
    privacyTitle: 'Privacidad – Qué recoge Pipkite y por qué',
    privacyDesc:
      'Qué guarda Pipkite en tu PC, qué reciben los servicios de licencia, revisión con IA y parsers, y qué no se recoge nunca. Explicado con claridad.',
    notFoundTitle: 'Página no encontrada | Pipkite',
    notFoundDesc: 'Esta página no existe. Vuelve a Pipkite, el copiador gratis de señales de Telegram a MT5.',
    ogAlt: `Pipkite: señales de Telegram a MetaTrader 5 en ${ms} ms`,
    appDescription:
      'App gratis para Windows que copia señales de trading de canales y grupos de Telegram a MetaTrader 5. Cada canal recibe un parser creado por el equipo de Pipkite; las señales se comprueban con tus reglas de riesgo y se ejecutan en unos 4 ms.',
    featureList: [
      'Copia automática de señales de canales y grupos de Telegram a MetaTrader 5',
      `Unos ${ms} ms desde el mensaje de Telegram hasta la orden enviada (mediana, medida)`,
      'Parsers creados para cada canal por el equipo de Pipkite, entregados como actualizaciones automáticas',
      'Revisión consultiva con IA de cada señal: veredicto Buena, Mala o Incierta con motivos',
      'Tamaño por lotes fijos o % de riesgo, límites de spread y deslizamiento, horario de trading',
      'Espera a las zonas de entrada y puede forzar la entrada si el precio las rebasa por poco',
      'Varios take-profits, objetivos divididos',
      'Pausa y reanudación de la copia con un clic',
      'Registro de órdenes a prueba de fallos con reconciliación',
      'Panel de beneficios y latencia en directo',
    ],
  },

  common: {
    skip: 'Ir al contenido',
    home: 'Inicio',
    download: 'Descargar',
    freeKey: 'Clave gratis',
    downloadWindows: 'Descargar para Windows',
    getFreeKey: 'Obtener una clave gratis',
    getYourFreeKey: 'Obtén tu clave gratis',
    copy: 'Copiar',
    copied: 'Copiado',
    themeToggle: 'Cambiar tema',
    openMenu: 'Abrir menú',
    language: 'Idioma',
    mainNav: 'Principal',
    mobileNav: 'Móvil',
    breadcrumb: 'Ruta de navegación',
    onThisPage: 'En esta página',
    illustration: 'Ilustración',
    homeAria: 'Inicio de Pipkite',
  },

  nav: { how: 'Cómo funciona', features: 'Funciones', speed: 'Velocidad', ai: 'IA', guide: 'Guía', faq: 'Preguntas' },

  footer: {
    tagline: 'Copiador gratis de señales de Telegram a MetaTrader 5 para Windows, con parsers creados para tus canales.',
    versionLine: (v: string) => `Versión ${v} · Windows 10 / 11, 64 bits`,
    product: 'Producto',
    help: 'Ayuda',
    features: 'Funciones',
    performance: 'Rendimiento',
    changelog: 'Novedades',
    guide: 'Cómo copiar señales de Telegram a MT5',
    faq: 'Preguntas frecuentes',
    privacy: 'Privacidad',
    riskTitle: 'Aviso de riesgo.',
    risk: 'Operar con productos apalancados como forex y CFD conlleva un alto riesgo de pérdida. Pipkite copia señales; no puede garantizar ejecuciones ni beneficios, y la revisión con IA es una primera versión que puede equivocarse. Prueba primero en una cuenta demo.',
    legal: (year: number) =>
      `© ${year} Pipkite. MetaTrader es una marca de MetaQuotes Ltd. Telegram es una marca de Telegram FZ-LLC. Pipkite no está afiliado a ninguna de ellas.`,
  },

  hero: {
    badgeNew: 'Nuevo',
    badge: (v: string) => `Ya está aquí la versión ${v}. Gratis para Windows`,
    title: 'Señales de Telegram en MetaTrader 5 en',
    unit: 'ms',
    lead: 'Pipkite lee los canales que eliges, entiende cada señal, la comprueba con tus reglas y coloca la orden en MT5 antes de que puedas desbloquear el móvil.',
    leadStrong: '100 % gratis.',
    checks: ['Gratis para siempre, sin tarjeta', 'Parsers hechos para ti', 'Sin permisos de administrador', 'Windows 10 / 11'],
    shotAlt: 'El panel de Pipkite: conexiones, cuenta y beneficios, feed en directo y latencia',
    cardNow: 'ahora',
    aiReview: 'Revisión IA',
    good: 'Buena',
    rr: 'beneficio/riesgo 2,2',
    orderSent: 'Orden enviada a MT5',
    downloadsCount: '{n} descargas',
  },

  how: {
    eyebrow: 'Cómo funciona',
    title: 'Del mensaje a la orden sin mover un dedo',
    lead: 'Se acabó copiar y pegar precios y perder entradas mientras duermes. Esto es lo que pasa cuando llega una señal.',
    step1: '1 · Llega el mensaje',
    step2: '2 · Entendida y comprobada',
    step3: '3 · Orden en MetaTrader 5',
    fields: { symbol: 'Símbolo', side: 'Dirección', entry: 'Entrada', entryNote: 'espera a la zona', sl: 'Stop-loss', tps: 'Take-profits' },
    aiGood: 'IA: Buena',
    checksOk: 'Spread OK · Riesgo OK',
    volume: 'Volumen',
    sent: 'Enviada',
    illustration: 'Ilustración. El tamaño, los objetivos y la entrada siguen tus propias reglas.',
    setupTitle: 'Configúralo una vez, con un asistente guiado',
    steps: [
      { title: 'Conecta Telegram', text: 'Inicia sesión con tu propia cuenta de Telegram y marca los canales o grupos a copiar. Los códigos de acceso nunca se guardan.' },
      { title: 'Conecta MetaTrader 5', text: 'Pulsa «Start MetaTrader». Pipkite instala su Expert Advisor, abre tu terminal y se empareja con él.' },
      { title: 'Pide tus parsers', text: 'Un clic por canal. Nuestro equipo crea un parser para el formato exacto de ese canal y se instala solo, sin reiniciar.' },
      { title: 'Fija tus reglas', text: 'Lotes o % de riesgo, spread máximo, horario, zonas de entrada. La copia funciona en cuanto MT5 está emparejado; pausa o reanuda con un clic.' },
    ],
  },

  features: {
    eyebrow: 'Funciones',
    title: 'Todo lo que necesita un copiador serio',
    lead: 'C++ nativo en Windows. Sin extensiones de navegador, sin relés en la nube entre tú y tu bróker, sin cuotas mensuales.',
    parserTitle: 'Entiende señales desordenadas',
    parserText:
      'Emojis, Markdown, dígitos arábigo-índicos o en superíndice, guiones raros, varios take-profits. Cada canal recibe un parser creado por nuestro equipo para su formato; normaliza el mensaje, relaciona cada valor con el texto original y no adivina con comentarios, ediciones ni «TP1 alcanzado».',
    legend: { symbol: 'símbolo', side: 'dirección', label: 'etiqueta', ignored: 'ignorado' },
    entry: 'entrada',
    valid: 'Válida',
    rangeTitle: 'Espera tu entrada',
    rangeText: 'Con «buy 4299–4295» espera a que el precio entre en la zona y reacciona a cada cotización. La entrada forzada atrapa operaciones que la rebasan por poco.',
    filled: 'ejecutada',
    small: [
      { title: 'Ejecuta con tus reglas', text: 'Lotes fijos o % de riesgo (siempre redondeado a la baja), límites de spread y deslizamiento, horario, límites de cuenta, take-profits divididos.' },
      { title: 'A prueba de fallos', text: 'Cada orden se registra en disco antes de salir. Tras un fallo o un corte de luz, las órdenes dudosas se reconcilian, nunca se reenvían a ciegas.' },
      { title: 'Pausa y reanuda con un clic', text: 'La copia funciona en cuanto MT5 está emparejado. Un clic detiene al instante las nuevas operaciones y las reanuda igual de rápido. La pausa nunca cierra posiciones abiertas.' },
      { title: 'Parsers hechos para ti', text: 'Nada que configurar: pide un parser para un canal y nuestro equipo lo crea para ese formato exacto. Llega como actualización firmada y se carga sin reiniciar.' },
    ],
    dashTitle: 'Un panel que dice la verdad',
    dashText:
      'Saldo, equity, P/L flotante y realizado neto de comisiones, un feed en directo de cada señal y su resultado, y percentiles de latencia medidos en tu propio PC. Los datos del bróker mandan.',
    equity: 'Equity',
    floating: 'P/L flotante',
    latency: 'Latencia p95',
  },

  speed: {
    eyebrow: 'Velocidad',
    title: 'Medido, no prometido.',
    leadHtml: `En el copy trading, cada milisegundo es precio. Todo el recorrido de Pipkite, desde el mensaje de Telegram hasta la orden que sale hacia tu bróker, funciona en código nativo con una sola escritura duradera, y tarda unos <strong>${ms} ms</strong>.`,
    proof: 'No hace falta que nos creas: el panel muestra en directo los percentiles de latencia medidos en tu propio PC.',
    stats: [
      { label: 'Telegram → envío a MT5', note: `tiempo propio de Pipkite, mediana (p95 ${p95} ms)` },
      { label: 'Para leer una señal', note: `tiempo de análisis mediano, p95 ${num(L, BENCH.parseP95Us, 1)} µs` },
      { label: 'Señales por segundo', note: 'rendimiento del parser sin conexión' },
      { label: 'Escritura en disco por orden', note: `antes ${BENCH.flushesBefore}, sigue siendo a prueba de fallos` },
    ],
    chartTitle: 'Mensaje de Telegram → orden enviada a MT5',
    chartSub: 'Mediana, en milisegundos. Menos es mejor.',
    fastest: 'El más rápido probado',
    rowUs: 'Pipkite 1.0',
    rowBefore: 'Antes de optimizar',
    improvementStrong: '3,6× más rápido que nuestra primera versión.',
    improvement: 'Una escritura en disco por orden en lugar de cuatro, las escrituras críticas van primero y las zonas de entrada reaccionan a cada cotización en lugar de a un temporizador de 100 ms.',
    reference: `Equipo de referencia: ${BENCH.machine}, compilación Release, registro duradero activado. La entrega de Telegram, tu bróker y tu red añaden su propio tiempo.`,
  },

  ai: {
    exampleLabel: 'Ejemplo de revisión con IA',
    reasons: ['El stop-loss está en el lado correcto de la entrada', 'Beneficio/riesgo hasta el último objetivo: 2,2 : 1', 'El primer objetivo está más cerca que el stop-loss'],
    good: 'Buena',
    eyebrow: 'IA incluida',
    title: 'Una segunda opinión sobre cada señal, gratis',
    lead: 'El backend de IA de Pipkite ya está funcionando. Nada que instalar ni clave de API que comprar: añade tu clave de licencia gratis y cada señal recibe una segunda opinión.',
    points: [
      { title: 'Veredictos claros y razonados', text: 'Buena, Mala o Incierta, con los motivos: posición del stop, beneficio/riesgo y amplitud de la entrada.' },
      { title: 'Consejo, no órdenes', text: 'El veredicto aparece junto a cada señal en el feed en directo para ayudarte a juzgar a un proveedor. Actívalo o desactívalo en Execution policy.' },
      { title: 'Solo se envía la señal', text: 'Símbolo, dirección, entrada, SL y TP. Nunca tu cuenta, saldo, acceso al bróker, chats ni el texto de los mensajes. Las señales se guardan para que la IA aprenda.' },
    ],
    warnStrong: 'Aún es pronto: la IA puede equivocarse.',
    warn: 'Es una primera versión, así que se le pueden escapar cosas, y su puntuación es un índice de confianza, no una promesa de beneficio. La mejoramos en cada versión; tómala como una segunda opinión, nunca como una garantía.',
  },

  screens: {
    eyebrow: 'Echa un vistazo',
    title: 'Limpia, tranquila y fácil de leer',
    lead: 'Temas oscuro y claro (cambia el tema de esta web para ver ambos), una ayuda en cada botón y una guía completa integrada.',
    tablist: 'Pantallas de la app',
    screenAlt: (label: string, text: string) => `Pantalla ${label} de Pipkite. ${text}`,
    items: {
      dashboard: { label: 'Panel', text: 'Conexiones, cuenta y beneficios, feed en directo y latencia, en una sola pantalla.' },
      history: { label: 'Historial', text: 'Cada mensaje, cómo se leyó y qué se hizo, con el motivo. Exporta a CSV o JSON.' },
      metatrader: { label: 'MetaTrader', text: 'Abre MT5 con el EA, aprueba el emparejamiento y elige el símbolo de cada canal.' },
      'execution-policy': { label: 'Reglas de ejecución', text: 'Tamaño, take-profits, tiempos y límites. Cada opción tiene un «?» que la explica.' },
      'signal-rules': { label: 'Parsers', text: 'Mira qué parser lee cada canal y pide uno a medida con un clic.' },
    },
    note: 'La interfaz de la app está en inglés.',
  },

  free: {
    eyebrow: 'Precio',
    title: 'Gratis. Ni prueba, ni freemium.',
    lead: 'Sin suscripción, sin tarjeta, sin plan «pro» que esconda lo mejor. Descárgalo, consigue tu clave gratis y úsalo todo.',
    price: '0 €',
    forever: 'para siempre',
    included: [
      'La app completa, sin límites',
      'Canales y señales de Telegram ilimitados',
      'Parsers hechos para tus canales',
      'Revisión con IA de cada señal',
      'Actualizaciones automáticas firmadas',
      'Panel de beneficios y latencia en directo',
    ],
    privacyEyebrow: 'Privacidad y seguridad',
    privacyTitle: 'Gratis no significa que tú seas el producto',
    privacy: [
      { title: 'Tus datos se quedan en tu PC', text: 'Historial, ajustes y registros viven en una base de datos local. Los secretos se cifran con Windows DPAPI.' },
      { title: 'Nunca tus contraseñas del bróker', text: 'Tu acceso al bróker se queda en MetaTrader. Pipkite nunca lo pide.' },
      { title: 'Módulos y actualizaciones firmados', text: 'Cada módulo de parser y cada versión van firmados con ECDSA y se verifican antes de cargarse.' },
    ],
  },

  faq: {
    eyebrow: 'Preguntas',
    title: 'Preguntas y respuestas',
    leadHtml:
      '¿Nuevo en esto? Lee la <a href="/telegram-to-mt5-copier">guía paso a paso</a>. La app incluye además una guía completa con capturas de cada pantalla.',
    items: [
      {
        q: '¿Qué es Pipkite?',
        a: `Pipkite es una app gratis para Windows que copia señales de trading de canales y grupos de Telegram a MetaTrader 5. Lee cada mensaje nuevo con un parser creado para ese canal, extrae el símbolo, la dirección, la entrada, el stop-loss y los take-profits, los comprueba con tus reglas de riesgo y coloca la orden en tu propio terminal MT5 en unos ${ms} ms. Una revisión opcional con IA da una segunda opinión sobre cada señal.`,
      },
      {
        q: '¿Pipkite es realmente gratis?',
        a: 'Sí. La app, los parsers, la revisión con IA y las actualizaciones son gratis, sin suscripción, prueba ni tarjeta. La clave de licencia también es gratis; identifica tu instalación ante los servicios de parsers, IA y actualizaciones.',
      },
      {
        q: '¿Cómo copio señales de Telegram a MT5 automáticamente?',
        a: 'Instala Pipkite, inicia sesión con tu cuenta de Telegram y marca los canales a copiar, pulsa «Start MetaTrader» para adjuntar el Expert Advisor de Pipkite y aprueba el emparejamiento, pide un parser para cada canal y fija tu tamaño de lote o % de riesgo. En cuanto el parser de un canal está instalado, sus nuevas señales se colocan solas en MT5. Pausa o reanuda la copia con un clic.',
      },
      {
        q: '¿Qué tan rápido es, de verdad?',
        a: `La parte propia de Pipkite, desde que Telegram entrega el mensaje hasta que la orden se envía a MT5, tarda unos ${ms} ms (mediana) en el PC de referencia. La entrega de Telegram, tu bróker y tu conexión añaden su propio tiempo. El panel muestra la latencia medida en tu equipo.`,
      },
      {
        q: '¿Funciona con mi bróker?',
        a: 'Pipkite coloca las órdenes mediante un Expert Advisor dentro de tu propio terminal de escritorio MetaTrader 5 en Windows, así que funciona con los brókers con los que operas en MT5. Tu acceso se queda en MetaTrader. Asocia cada símbolo del canal (por ejemplo GOLD) al nombre de tu bróker (por ejemplo XAUUSD).',
      },
      { q: '¿Funciona con MetaTrader 4?', a: 'No. Pipkite solo es compatible con MetaTrader 5.' },
      {
        q: '¿Puede copiar de canales y grupos privados?',
        a: 'Sí. Pipkite inicia sesión en Telegram como tú, así que puede leer cualquier canal o grupo del que tu cuenta sea miembro, incluidos los privados. Solo se copian los mensajes que llegan después de iniciar sesión; el historial nunca se convierte en órdenes.',
      },
      {
        q: '¿Puedo probarlo sin arriesgar dinero?',
        a: 'Sí. Conecta primero una cuenta demo de MT5: la copia funciona exactamente igual sin nada en juego. El asistente también tiene un paso «Test a sample» que muestra lo que Pipkite lee de un mensaje pegado sin enviar nada. La copia está activa en cuanto MT5 está emparejado, y la pausa detiene al instante las nuevas operaciones.',
      },
      {
        q: '¿Tengo que configurar o escribir un parser?',
        a: 'No, y no puedes: los parsers los crea el equipo de Pipkite. Pulsa «Request a custom parser» en la pantalla Signal rules; se envían algunos mensajes recientes del canal sin enlaces, @usuarios, teléfonos ni correos, creamos un parser para su formato exacto y se instala solo como actualización firmada, sin reiniciar. Hasta que llega el parser de un canal, sus mensajes no se operan.',
      },
      {
        q: '¿La revisión con IA siempre acierta?',
        a: 'No. La revisión con IA es una primera versión y puede equivocarse. Evalúa cómo está construida cada señal (posición del stop, beneficio/riesgo, amplitud de entrada) y da una puntuación de confianza, que no es una promesa de beneficio. Se muestra como consejo junto a cada señal y la vamos mejorando con el tiempo.',
      },
      {
        q: '¿La IA ve mi cuenta?',
        a: 'No. Solo se envían los campos de la señal: símbolo, dirección, tipo de orden, entrada, stop-loss y take-profits. Nunca tu número de cuenta, saldo, acceso al bróker, chats ni el texto de los mensajes. No se conserva nada de la señal después de la respuesta.',
      },
      {
        q: '¿Qué idiomas están disponibles?',
        a: 'Esta web está disponible en inglés, francés, español y árabe. La app Pipkite en sí está por ahora en inglés. Los parsers funcionan con señales escritas en cualquier idioma, porque cada uno se crea para su canal.',
      },
      {
        q: '¿Mi PC tiene que estar encendido?',
        a: 'Sí. Pipkite y MetaTrader funcionan en tu PC con Windows, así que la suspensión, el apagado o un terminal MT5 cerrado pausan la copia. Pipkite puede mantener Windows despierto mientras copia; para copiar 24/7, usa un VPS con Windows.',
      },
      {
        q: 'Windows muestra un aviso de SmartScreen. ¿Es normal?',
        a: 'Esta primera versión aún no está firmada con un certificado de pago, así que Windows puede avisarte. Elige «Más información → Ejecutar de todas formas». Puedes verificar el archivo con la suma SHA-256 de la página de descarga.',
      },
    ],
  },

  cta: {
    title: 'Deja de perder entradas. Copia en milisegundos.',
    lead: 'Descarga gratis, clave gratis y un asistente de configuración guiado.',
  },

  download: {
    breadcrumb: 'Descargar',
    h1: 'Descarga el copiador gratis de Telegram a MT5',
    sub: (v: string) => `Pipkite ${v} para Windows 10 y 11. Publicado el`,
    whatsNew: 'Novedades',
    installer: { title: 'Instalador', badge: 'Recomendado', text: 'Se instala solo para ti, sin permisos de administrador. Añade un acceso directo al menú Inicio y la guía de usuario.' },
    portable: { title: 'Zip portátil', text: 'Descomprímelo donde puedas escribir y ejecuta Pipkite.Launcher.exe. No se instala nada.' },
    sha: 'SHA-256',
    checkHint: 'Comprueba una descarga en PowerShell:',
    stepsTitle: 'Listo en tres pasos',
    steps: [
      { title: 'Ejecuta el instalador', text: `Abre ${DOWNLOADS.installer.name}. Si aparece SmartScreen, elige «Más información → Ejecutar de todas formas».` },
      { title: 'Añade tu clave gratis', text: 'El asistente de bienvenida te pide la clave de licencia. Pégala y pulsa «Validate & save».' },
      { title: 'Sigue el asistente', text: 'Conecta Telegram, abre MetaTrader, elige tus canales, pide sus parsers y prueba una señal.' },
    ],
    noKey: '¿Aún no tienes clave? Consigue una gratis',
    reqTitle: 'Requisitos del sistema',
    req: [
      'Windows 11 o Windows 10 22H2, 64 bits',
      '2 núcleos o más, 4 GB de RAM, SSD recomendado',
      'MetaTrader 5, con sesión iniciada en tu bróker',
      'Una cuenta de Telegram',
      '.NET Framework 4.8 (incluido en Windows 10/11)',
    ],
    reqNote: 'La suspensión, el apagado o un terminal MT5 cerrado pausan la copia. Para copiar 24/7, usa un VPS con Windows. La interfaz de la app está en inglés.',
  },

  license: {
    breadcrumb: 'Clave de licencia gratis',
    h1: 'Tu clave, gratis y para siempre',
    lead: 'Sin pago y sin tarjeta. La clave conecta tu instalación con los servicios en línea de Pipkite:',
    unlocks: ['Parsers hechos para tus canales', 'Revisión con IA de cada señal', 'Actualizaciones automáticas firmadas'],
    skipHtml: 'La app se abre sin clave, pero los parsers, la revisión con IA y las actualizaciones la necesitan. Añádela en el asistente de bienvenida o más tarde en <strong>Settings → AI and licensing</strong>.',
    name: 'Tu nombre',
    namePh: 'Ana Trader',
    email: 'Correo electrónico',
    emailOpt: '(opcional, te enviaremos la clave)',
    emailPh: 'tu@ejemplo.com',
    submit: 'Generar mi clave gratis',
    generating: 'Generando…',
    detailsUse: 'Solo usamos tus datos para identificar tu clave.',
    privacy: 'Privacidad',
    ready: 'Tu clave de licencia está lista',
    copyKey: 'Copiar clave',
    saveIt: 'Guárdala en un lugar seguro. Solo se muestra una vez.',
    emailSentHtml: 'También la enviamos a <strong></strong>. Revisa la carpeta de spam si no llega en unos minutos.',
    emailNone: 'No escribiste un email, así que esta es la única copia. Si la pierdes, tendrás que generar una clave nueva.',
    done: 'Listo',
    afterHtml: [
      'Abre Pipkite (o <a href="/download">descárgalo</a>).',
      'Pega la clave en el asistente de bienvenida o en Settings → AI and licensing.',
      'Pulsa <strong>Validate &amp; save</strong>. El estado se pone en verde.',
    ],
    errors: {
      name: 'Escribe tu nombre.',
      email: 'Ese correo electrónico no parece correcto.',
      bad: 'Revisa tus datos e inténtalo de nuevo.',
      rate: 'Demasiadas solicitudes. Espera un minuto e inténtalo de nuevo.',
      server: 'El servicio de licencias tiene problemas. Inténtalo de nuevo en un momento.',
      unexpected: 'El servicio de licencias devolvió una respuesta inesperada.',
      network: 'No se pudo contactar con el servicio de licencias. Revisa tu conexión e inténtalo de nuevo.',
    },
  },

  guide: {
    breadcrumb: 'Guía de Telegram a MT5',
    h1: 'Cómo copiar señales de Telegram a MetaTrader 5 automáticamente',
    lead: 'Una guía completa y práctica: de una señal publicada en un canal de Telegram a una orden en tu terminal MT5, sin copiar precios a mano.',
    updated: 'Actualizado el',
    by: 'Por el equipo de Pipkite',
    readTime: '7 min de lectura',
    shortTitle: 'Respuesta corta',
    short: `Para copiar señales de Telegram a MT5 automáticamente, instala un copiador de Telegram a MT5 como Pipkite, inicia sesión con tu cuenta de Telegram, elige los canales a copiar, adjunta el Expert Advisor del copiador a tu terminal MetaTrader 5, pide un parser para cada canal y fija tu tamaño de lote o % de riesgo. Cada nueva señal se analiza y se coloca como orden por sí sola, en unos ${ms} ms. Un clic pausa o reanuda la copia.`,
    figures: {
      metatrader: { alt: 'Pantalla MetaTrader de Pipkite con la lista de terminales, el botón Start MetaTrader y el estado del emparejamiento', caption: 'La pantalla MetaTrader: abre el terminal, aprueba el emparejamiento, asocia símbolos.' },
      'signal-rules': { alt: 'Pantalla Signal rules de Pipkite con los canales, el estado de su parser y el botón Request a custom parser', caption: 'Signal rules: el parser de cada canal y el botón para pedir uno.' },
      'execution-policy': { alt: 'Pantalla Execution policy de Pipkite con tamaño de posición, take-profits y tiempos', caption: 'Execution policy: cuánto, qué objetivos y cuándo no operar.' },
    },
    sections: [
      {
        id: 'what-is',
        title: '¿Qué es un copiador de Telegram a MT5?',
        html: `<p>Un copiador de Telegram a MT5 es un programa que vigila canales o grupos de Telegram y convierte cada señal de trading en una orden en MetaTrader 5. Una señal como «GOLD BUY 4299–4295, SL 4282, TP 4307» se lee, se extraen el símbolo, la dirección, la entrada, el stop-loss y los take-profits, tus propias reglas deciden el tamaño y la orden se envía a tu bróker a través de MT5.</p>
<p>Copiar a mano es lento y propenso a errores: para cuando desbloqueas el móvil, lees el mensaje y escribes la orden, el precio ya se ha movido. Un copiador elimina ese retraso y los errores de tecleo, y sigue trabajando mientras duermes.</p>`,
      },
      {
        id: 'need',
        title: 'Qué necesitas',
        html: `<ul>
<li>Un PC con Windows 10 (22H2) o Windows 11, de 64 bits, con 4 GB de RAM. Un VPS con Windows sirve para copiar 24/7.</li>
<li>MetaTrader 5 (escritorio) con sesión iniciada en tu bróker. Una cuenta demo es lo mejor para empezar.</li>
<li>Tu cuenta de Telegram, miembro de los canales o grupos que quieres copiar.</li>
<li>Un API ID y un hash de Telegram gratis en <code>my.telegram.org</code> (un minuto).</li>
<li>Pipkite y una <a href="/license">clave de licencia gratis</a>, necesaria para pedir parsers y recibirlos como actualizaciones (y para la revisión con IA).</li>
</ul>`,
      },
      {
        id: 'step-1',
        title: 'Paso 1: instala Pipkite',
        html: `<p><a href="/download">Descarga el instalador</a> y ejecútalo. Se instala solo para tu usuario de Windows, sin permisos de administrador. Si aparece Windows SmartScreen, elige <strong>Más información → Ejecutar de todas formas</strong>; puedes comprobar el archivo con la suma SHA-256 de la página de descarga. Al abrirlo por primera vez, un breve asistente te guía por cada paso de abajo, con un estado en directo para cada uno. La interfaz de la app está en inglés.</p>`,
      },
      {
        id: 'step-2',
        title: 'Paso 2: conecta Telegram',
        html: `<ol>
<li>Ve a <code>my.telegram.org</code>, abre <strong>API development tools</strong> y copia el API ID y el API hash.</li>
<li>Pega ambos en Pipkite y pulsa <strong>Save credentials</strong>. Se cifran en tu PC con Windows DPAPI y nunca se registran.</li>
<li>Escribe tu número, pulsa <strong>Send code</strong> e introduce el código que te envía Telegram (y tu contraseña de verificación en dos pasos si la usas).</li>
<li>Marca los canales y grupos a copiar y pulsa <strong>Add checked as sources</strong>. Los canales privados también funcionan, siempre que tu cuenta sea miembro.</li>
</ol>
<p>Solo se copian los mensajes que llegan después de iniciar sesión. El historial nunca se convierte en órdenes.</p>`,
      },
      {
        id: 'step-3',
        title: 'Paso 3: conecta MetaTrader 5',
        figure: 'metatrader',
        html: `<ol>
<li>En la pantalla <strong>MetaTrader</strong>, elige tu terminal y pulsa <strong>Start MetaTrader</strong>. Pipkite instala su Expert Advisor, abre MT5 con él en un gráfico y activa los ajustes necesarios. Antes te lo pregunta.</li>
<li>Inicia sesión en tu bróker dentro de MetaTrader como siempre. Pipkite nunca pide la contraseña del bróker.</li>
<li>De vuelta en Pipkite, pulsa <strong>Approve pairing</strong> y escribe tu número de cuenta para confirmar. Empareja primero una <strong>cuenta demo</strong>: en cuanto hay una cuenta emparejada, la copia está activa.</li>
<li>Asocia el símbolo de cada canal al nombre de tu bróker, por ejemplo «GOLD» del canal a «XAUUSD» de tu bróker.</li>
</ol>`,
      },
      {
        id: 'step-4',
        title: 'Paso 4: pide un parser para cada canal',
        figure: 'signal-rules',
        html: `<p>Cada proveedor escribe sus señales a su manera, así que cada canal necesita un <em>parser</em> creado para su formato. Pipkite no trae ninguno, y no los creas ni los editas tú: lo hace el equipo de Pipkite.</p>
<ol>
<li>Abre <strong>Signal rules</strong>, selecciona un canal y pulsa <strong>Request a custom parser for this channel</strong>. Añade una nota si quieres.</li>
<li>Pipkite envía algunos mensajes recientes de ese canal, sin enlaces, @usuarios, teléfonos ni correos.</li>
<li>Creamos el parser y lo publicamos como actualización firmada. Se instala solo y funciona sin reiniciar; <strong>Check for parser updates</strong> lo descarga al momento.</li>
</ol>
<p>Hasta que llega el parser de un canal, sus mensajes se muestran como no compatibles y nunca se operan.</p>`,
      },
      {
        id: 'step-5',
        title: 'Paso 5: fija tus reglas de riesgo',
        figure: 'execution-policy',
        html: `<ul>
<li><strong>Tamaño de posición:</strong> un lote fijo, o un porcentaje del equity arriesgado hasta el stop-loss. El cálculo por riesgo siempre redondea a la baja al paso de lote de tu bróker; si incluso el lote mínimo es demasiado, la operación se omite en lugar de forzarse.</li>
<li><strong>Take-profits:</strong> un solo objetivo, o la posición repartida entre varios.</li>
<li><strong>Límites:</strong> antigüedad máxima de la señal, spread y deslizamiento máximos por símbolo, posiciones abiertas, pérdida diaria y horario de trading.</li>
<li><strong>Zonas de entrada:</strong> con señales «buy 4299–4295», Pipkite espera a que el precio entre en la zona. La opción <em>force-enter</em> abre la operación si el precio la rebasó por unos pocos pips que tú eliges.</li>
<li><strong>Revisión con IA:</strong> activa «review every signal» para ver una opinión Buena / Incierta / Mala junto a cada señal en el feed en directo.</li>
</ul>`,
      },
      {
        id: 'step-6',
        title: 'Paso 6: prueba y deja que copie',
        html: `<p>No hay un modo de vista previa aparte: en cuanto MT5 está emparejado y un canal tiene su parser, las señales válidas se copian. Así que prueba antes de arriesgar dinero:</p>
<ul>
<li>En el paso <strong>Test a sample</strong> del asistente, pega un mensaje real del canal para ver exactamente lo que Pipkite lee. No se envía nada.</li>
<li>Empieza en una <strong>cuenta demo</strong> y observa algunas señales reales en el panel y en el historial. Cada fila explica su decisión, así que una señal rechazada te dice por qué (sin stop-loss, símbolo sin asociar, spread demasiado amplio, etc.).</li>
<li>Usa <strong>Pause copying</strong> en la barra superior (o el icono de la bandeja) para detener al instante las nuevas operaciones, y <strong>Resume copying</strong> para continuar. La pausa nunca cierra posiciones abiertas.</li>
</ul>`,
      },
      {
        id: 'formats',
        title: '¿Qué formatos de señal funcionan?',
        html: `<p>Prácticamente cualquier formato que un proveedor use de forma constante, en cualquier idioma, porque el parser de cada canal se crea para su estilo. Los parsers manejan emojis, Markdown, guiones inusuales, dígitos arábigo-índicos y en superíndice y varios take-profits, e ignoran comentarios como «TP1 alcanzado» en lugar de adivinar. Si un proveedor cambia su forma de publicar, vuelve a pedir el parser y la versión actualizada llega sola.</p>`,
      },
      {
        id: 'ai',
        title: '¿Qué hace la revisión con IA?',
        html: `<p>La revisión con IA da una segunda opinión sobre cada señal: <strong>Buena</strong>, <strong>Incierta</strong> o <strong>Mala</strong>, con motivos como la posición del stop-loss, el beneficio/riesgo y la amplitud de la entrada. Solo se envían los campos de la señal, nunca tu cuenta ni tus mensajes.</p>
<p><strong>Puede equivocarse.</strong> Es una primera versión, y su puntuación es un índice de confianza, no una promesa de beneficio. Tómala como un consejo; mejora con cada versión.</p>`,
      },
      {
        id: 'speed',
        title: '¿Qué tan rápida es la copia?',
        html: `<p>El procesamiento propio de Pipkite, desde que Telegram entrega el mensaje hasta que la orden se envía a MT5, tarda unos <strong>${ms} ms</strong> (mediana; ${p95} ms en el percentil 95) en el PC de referencia (${BENCH.machine}), con una escritura de registro a prueba de fallos antes de cada orden. Leer una señal tarda unos ${num(L, BENCH.parseP50Us, 1)} µs. La entrega de Telegram, tu bróker y tu red añaden su propio tiempo, y el panel muestra la latencia medida en tu equipo. Más detalles en la <a href="/#speed">sección de rendimiento</a>.</p>`,
      },
      {
        id: 'safety',
        title: 'Con seguridad',
        html: `<ul>
<li>Empareja primero una cuenta demo; pasa a una cuenta real solo cuando los resultados sean los que esperas.</li>
<li>Mantén siempre una regla de stop-loss y límites de spread por símbolo. Los símbolos sin límites no se operan.</li>
<li>Mantén tu PC despierto (Pipkite puede hacerlo) o usa un VPS: la suspensión y el apagado pausan la copia.</li>
<li>Ningún copiador puede garantizar ejecuciones ni beneficios, y la revisión con IA puede equivocarse.</li>
</ul>`,
      },
      {
        id: 'troubleshooting',
        title: 'Solución de problemas',
        html: `<dl>
<dt>«Telegram: Not set up»</dt><dd>Introduce tu API ID y hash, y después inicia sesión con tu número.</dd>
<dt>«MT5 offline»</dt><dd>MetaTrader no está abierto o el Expert Advisor no está en un gráfico. Pulsa Start MetaTrader; si MT5 ya está abierto, ciérralo y vuelve a pulsar Start.</dd>
<dt>Una señal aparece como «Unsupported»</dt><dd>Ese canal aún no tiene parser. Pide uno en la pantalla Signal rules.</dd>
<dt>Una señal aparece como Rejected o Review</dt><dd>Abre la fila en el historial para ver por qué. No se envió nada.</dd>
<dt>La solicitud de emparejamiento nunca aparece</dt><dd>Abre MetaTrader e inicia sesión en tu bróker primero; después aprueba el emparejamiento.</dd>
</dl>`,
      },
    ],
    ctaTitle: '¿Listo para copiar tu primera señal?',
    ctaText: 'Descarga gratis, clave gratis y un asistente que hace el trabajo pesado.',
    ctaButton: 'Descargar gratis',
  },

  changelog: {
    breadcrumb: 'Novedades',
    h1: 'Novedades',
    lead: 'Lo nuevo en cada versión de Pipkite.',
    version: (v: string) => `Versión ${v}`,
    firstRelease: 'Primera versión',
    downloadButton: (v: string) => `Descargar ${v}`,
    groups: [
      {
        h: 'Ejecución',
        items: [
          'Motor nativo en C++ que nunca pierde una señal en silencio, ni siquiera con mucha carga.',
          'Gestión de la entrada: esperar a la zona, entrar dentro de una tolerancia o solo órdenes pendientes. BUY al ask, SELL al bid.',
          'Tamaño por lotes fijos o % de riesgo siempre redondeado a la baja, límites de spread y desviación, objetivos divididos, horario y límites de cuenta.',
          'Registro duradero antes de cada orden; los resultados dudosos se reconcilian, nunca se reintentan a ciegas.',
          'Opción de entrada forzada para operaciones que rebasan por poco la zona de entrada.',
          'La copia está activa en cuanto se empareja una cuenta MT5; pausa / reanuda con un clic (barra superior o bandeja).',
        ],
      },
      {
        h: 'Velocidad',
        items: [
          `Una escritura en disco por orden en lugar de cuatro: el tiempo de Telegram a MT5 baja de ${num(L, BENCH.ownOverheadBeforeP50Ms, 1)} ms a ${ms} ms (mediana).`,
          'Las zonas de entrada reaccionan a cada cotización; el EA recoge las órdenes en cada tick además de con su temporizador de 10 ms.',
          'Mantiene Windows despierto mientras copia (opcional).',
        ],
      },
      {
        h: 'Parsers y fuentes',
        items: [
          'Copia de muchos canales y grupos, cada uno con su parser, política de entrada y ajustes de tamaño.',
          'Los parsers los crea el equipo de Pipkite: pide uno por canal desde la app y llega como actualización firmada que se carga sin reiniciar.',
          'Los parsers manejan emojis, Markdown, guiones raros, dígitos arábigo-índicos y en superíndice, y detectan comentarios y ediciones.',
        ],
      },
      {
        h: 'Revisión con IA',
        items: [
          'Opinión Buena / Incierta / Mala con motivos para cada señal, mostrada como consejo en el feed en directo.',
          'Primera versión: puede equivocarse y mejorará con el tiempo.',
          'Solo se envían los campos de la señal: nunca datos de la cuenta, chats ni el texto de los mensajes.',
        ],
      },
      {
        h: 'Panel, datos y seguridad',
        items: [
          'Feed en directo, tarjetas de cuenta y de beneficios del copiador, gráficos de equity y P/L, percentiles de latencia.',
          'Base de datos local con copias de seguridad verificadas y cifrado opcional con contraseña.',
          'Módulos y actualizaciones firmados (ECDSA P-256), secretos protegidos con DPAPI, sin registrar credenciales.',
          'Temas oscuro y claro cuidados, ayudas en cada botón, guía de usuario integrada.',
        ],
      },
    ],
  },

  privacy: {
    breadcrumb: 'Privacidad',
    h1: 'Privacidad',
    updated: 'Última actualización',
    shortTitle: 'En resumen',
    short:
      'Tus datos de trading, tu sesión de Telegram y tu acceso al bróker se quedan en tu PC. Los servicios en línea solo reciben lo necesario: tu nombre y un correo opcional para la clave de licencia, los campos de la señal para la revisión con IA y mensajes de ejemplo depurados cuando pides un parser. No vendemos datos, y esta web no tiene anuncios, cookies ni seguimiento: solo cuenta de forma anónima las páginas vistas y los clics de descarga.',
    sections: [
      {
        h: 'Lo que se queda en tu PC',
        html: `<ul>
<li>El historial de señales, las órdenes, los ajustes y los registros, en una base de datos local de tu perfil de Windows.</li>
<li>Tu API ID/hash y tu sesión de Telegram, cifrados con Windows DPAPI. Los códigos de acceso y las contraseñas de dos pasos se pasan a Telegram y nunca se guardan.</li>
<li>Tu acceso al bróker, que se queda en MetaTrader. Pipkite nunca lo pide.</li>
</ul>`,
      },
      {
        h: 'Claves de licencia',
        html: '<p>Cuando generas una clave gratis en esta web guardamos tu nombre, tu correo si lo das, un identificador de solicitud aleatorio, la clave y la fecha de emisión. Los usamos para identificar tu clave y, si diste un correo, para enviarte la clave al momento (desde support@pipkite.com mediante Microsoft 365) y ayudarte a recuperarla.</p>',
      },
      {
        h: 'Revisión con IA',
        html: '<p>Si la revisión con IA está activada, la app envía el símbolo, la dirección, el tipo de orden, la entrada, el stop-loss y los take-profits de la señal junto con tu clave de licencia. Se comprueba la clave, y la señal y su veredicto se guardan con tu clave para que la revisión con IA aprenda y mejore. Tu número de cuenta, saldo, acceso al bróker, chats y el texto de los mensajes nunca se envían.</p>',
      },
      {
        h: 'Solicitudes de parser',
        html: '<p>Cuando pulsas «Request a custom parser», la app envía el identificador y el título del canal, tu nota opcional y hasta 15 mensajes recientes de ese canal, sin enlaces, @usuarios, teléfonos ni correos. Se guardan con tu clave de licencia para crear el parser y entregártelo.</p>',
      },
      { h: 'Actualizaciones', html: '<p>La app envía tu clave de licencia al buscar y descargar actualizaciones, para que te lleguen los parsers correctos.</p>' },
      {
        h: 'Esta web',
        html: '<p>Sin anuncios, sin cookies y sin analítica de terceros. Para saber cuántas personas visitan y descargan, la web cuenta las páginas vistas (qué página, qué idioma, qué día) y los clics de descarga (instalador o zip). No se envía ni se guarda nada que te identifique: tu dirección IP solo se usa, en memoria y durante ese día, para contar una descarga una sola vez y evitar que scripts inflen las cifras. Si tu navegador envía Global Privacy Control o Do Not Track, no se cuenta nada. Tu elección de tema (claro u oscuro) se guarda solo en tu navegador. Nuestro proveedor de hosting puede conservar registros estándar del servidor.</p>',
      },
      {
        h: 'Tus opciones',
        html: `<ul>
<li>La app se abre sin clave de licencia; los parsers, la revisión con IA y las actualizaciones la necesitan.</li>
<li>La revisión con IA se puede desactivar en Execution policy.</li>
<li>Puedes borrar el historial local cuando quieras en Settings.</li>
<li>Para saber qué datos tenemos sobre tu clave de licencia o pedir que se borren, {contact}</li>
</ul>`,
      },
    ],
    contactEmail: (email: string) => `escribe a <a href="mailto:${email}">${email}</a>.`,
    contactFallback: 'contáctanos desde la app.',
    englishPrevails: 'Esta página es una traducción; en caso de discrepancia, prevalece la versión en inglés.',
  },

  notFound: {
    code: '404',
    title: 'Esta señal no se pudo leer',
    lead: 'La página que buscas no existe o se ha movido.',
    home: 'Volver al inicio',
    guide: 'Leer la guía',
  },
};
