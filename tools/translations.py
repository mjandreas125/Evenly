"""UI translations. The key is the English text the app passes to L(ru, en); the values are, in this order:
et uk de fr es it pt pl nl sv fi tr.

    python tools/translations.py          write Source/app/Translations.inc and check that nothing is missing
"""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from extract_strings import pairs  # noqa: E402

ORDER = ["et", "uk", "de", "fr", "es", "it", "pt", "pl", "nl", "sv", "fi", "tr"]

T = {
    "Open": ["Ava", "Відкрити", "Öffnen", "Ouvrir", "Abrir", "Apri", "Abrir", "Otwórz", "Openen", "Öppna", "Avaa", "Aç"],
    "Processing on": ["Töötlus sees", "Обробка ввімкнена", "Verarbeitung an", "Traitement activé", "Procesamiento activado", "Elaborazione attiva", "Processamento ativado", "Przetwarzanie włączone", "Verwerking aan", "Bearbetning på", "Käsittely päällä", "İşleme açık"],
    "Quit": ["Välju", "Вийти", "Beenden", "Quitter", "Salir", "Esci", "Sair", "Zakończ", "Afsluiten", "Avsluta", "Lopeta", "Çık"],
    "automatic": ["ise", "саме", "von selbst", "tout seul", "solo", "da sola", "sozinho", "samo", "vanzelf", "av sig själv", "itsestään", "kendiliğinden"],
    "Pick an output": ["Vali väljund", "Оберіть вихід", "Ausgabe wählen", "Choisir une sortie", "Elige una salida", "Scegli un'uscita", "Escolha uma saída", "Wybierz wyjście", "Kies een uitgang", "Välj en utgång", "Valitse ulostulo", "Bir çıkış seçin"],
    "90 s ago": ["90 s tagasi", "90 с тому", "vor 90 s", "il y a 90 s", "hace 90 s", "90 s fa", "há 90 s", "90 s temu", "90 s geleden", "90 s sedan", "90 s sitten", "90 sn önce"],
    "now": ["praegu", "зараз", "jetzt", "maintenant", "ahora", "ora", "agora", "teraz", "nu", "nu", "nyt", "şimdi"],
    "target": ["siht", "ціль", "Ziel", "cible", "objetivo", "obiettivo", "meta", "cel", "doel", "mål", "tavoite", "hedef"],
    "Reset": ["Lähtesta", "Скинути", "Zurücksetzen", "Réinitialiser", "Restablecer", "Ripristina", "Redefinir", "Resetuj", "Herstellen", "Återställ", "Palauta", "Sıfırla"],
    "More": ["Veel", "Ще", "Mehr", "Plus", "Más", "Altro", "Mais", "Więcej", "Meer", "Mer", "Lisää", "Diğer"],
    "  (asleep)": ["  (puhkab)", "  (спить)", "  (im Ruhezustand)", "  (en veille)", "  (en reposo)", "  (inattivo)", "  (em repouso)", "  (uśpione)", "  (slaapt)", "  (vilar)", "  (lepotilassa)", "  (uyuyor)"],
    "  (not connected)": ["  (ühendamata)", "  (не підключено)", "  (nicht verbunden)", "  (non connecté)", "  (no conectado)", "  (non collegato)", "  (não conectado)", "  (niepodłączone)", "  (niet verbonden)", "  (inte ansluten)", "  (ei yhdistetty)", "  (bağlı değil)"],
    "Connect": ["Ühenda", "Підключити", "Verbinden", "Connecter", "Conectar", "Collega", "Conectar", "Połącz", "Verbinden", "Anslut", "Yhdistä", "Bağla"],
    "Switch": ["Lülita", "Перемкнути", "Wechseln", "Basculer", "Cambiar", "Passa", "Mudar", "Przełącz", "Overschakelen", "Byt", "Vaihda", "Geç"],
    "you picked it": ["valisid ise", "ви обрали", "von dir gewählt", "choisi par vous", "elegido por ti", "scelto da te", "escolhido por você", "wybrane przez ciebie", "door jou gekozen", "valt av dig", "valitsit itse", "senin seçimin"],
    "you picked it for this source": ["valisid selle allika jaoks", "ви обирали для цього джерела", "von dir für diese Quelle gewählt", "votre choix pour cette source", "elegido por ti para esta fuente", "scelto da te per questa sorgente", "escolhido por você para esta fonte", "wybrane przez ciebie dla tego źródła", "door jou gekozen voor deze bron", "valt av dig för den här källan", "valitsit tälle lähteelle", "bu kaynak için senin seçimin"],
    "learned for this source": ["selle allika jaoks õpitud", "запам’ятано для цього джерела", "für diese Quelle gelernt", "appris pour cette source", "aprendido para esta fuente", "appreso per questa sorgente", "aprendido para esta fonte", "zapamiętane dla tego źródła", "geleerd voor deze bron", "inlärt för den här källan", "opittu tälle lähteelle", "bu kaynak için öğrenildi"],
    "by the app": ["rakenduse järgi", "за програмою", "nach der App", "d'après l'application", "según la aplicación", "in base all'app", "pelo aplicativo", "według aplikacji", "op basis van de app", "efter appen", "sovelluksen mukaan", "uygulamaya göre"],
    "by the title": ["pealkirja järgi", "за назвою", "nach dem Titel", "d'après le titre", "según el título", "in base al titolo", "pelo título", "według tytułu", "op basis van de titel", "efter titeln", "otsikon mukaan", "başlığa göre"],
    ': "': [": „", ": «", ": „", ": «", ": «", ": «", ": «", ": „", ": „", ": ”", ": ”", ": “"],
    '"': ["“", "»", "“", "»", "»", "»", "»", "”", "”", "”", "”", "”"],
    "by the sound": ["heli järgi", "за звуком", "nach dem Klang", "d'après le son", "según el sonido", "in base al suono", "pelo som", "według dźwięku", "op basis van het geluid", "efter ljudet", "äänen mukaan", "sese göre"],
    "default for now": ["praegu vaikimisi", "поки за замовчуванням", "vorerst Standard", "par défaut pour l'instant", "predeterminado por ahora", "per ora predefinito", "padrão por enquanto", "na razie domyślny", "voorlopig standaard", "tills vidare standard", "toistaiseksi oletus", "şimdilik varsayılan"],
    "off": ["väljas", "вимкнено", "aus", "désactivé", "desactivado", "disattivato", "desativado", "wyłączone", "uit", "av", "pois", "kapalı"],
    "not used in this mode": ["selles režiimis ei kasutata", "у цьому режимі не потрібна", "in diesem Modus nicht verwendet", "inutile dans ce mode", "no se usa en este modo", "non usata in questa modalità", "não usado neste modo", "nieużywane w tym trybie", "niet gebruikt in deze modus", "används inte i det här läget", "ei käytössä tässä tilassa", "bu modda kullanılmaz"],
    "listening…": ["kuulan…", "слухаю…", "höre zu …", "j'écoute…", "escuchando…", "ascolto…", "ouvindo…", "słucham…", "luisteren…", "lyssnar…", "kuuntelen…", "dinliyorum…"],
    "less boom": ["vähem mürinat", "менше гулу", "weniger Dröhnen", "moins de grondement", "menos retumbo", "meno rimbombo", "menos estrondo", "mniej dudnienia", "minder dreun", "mindre dån", "vähemmän jyminää", "daha az gümbürtü"],
    "more body": ["rohkem täidlust", "більше низів", "mehr Fülle", "plus de corps", "más cuerpo", "più corpo", "mais corpo", "więcej pełni", "meer volume", "mer fyllighet", "enemmän täyteläisyyttä", "daha dolgun"],
    "more clarity": ["rohkem selgust", "більше чіткості", "mehr Klarheit", "plus de clarté", "más claridad", "più chiarezza", "mais clareza", "więcej klarowności", "meer helderheid", "mer klarhet", "enemmän selkeyttä", "daha net"],
    "less harshness": ["vähem teravust", "менше різкості", "weniger Schärfe", "moins de dureté", "menos aspereza", "meno asprezza", "menos aspereza", "mniej ostrości", "minder scherpte", "mindre skärpa", "vähemmän terävyyttä", "daha az keskinlik"],
    "fuller mids": ["täidlasem keskpiirkond", "насиченіша середина", "vollere Mitten", "médiums plus pleins", "medios más llenos", "medi più pieni", "médios mais cheios", "pełniejsza średnica", "vollere middentonen", "fylligare mellanregister", "täyteläisemmät keskiäänet", "daha dolgun orta frekanslar"],
    "softer mids": ["pehmem keskpiirkond", "м’якша середина", "weichere Mitten", "médiums plus doux", "medios más suaves", "medi più morbidi", "médios mais suaves", "łagodniejsza średnica", "zachtere middentonen", "mjukare mellanregister", "pehmeämmät keskiäänet", "daha yumuşak orta frekanslar"],
    "a small tone touch-up": ["väike tooni kohendus", "невелика правка тону", "eine kleine Klangkorrektur", "une petite retouche du timbre", "un pequeño retoque de tono", "un piccolo ritocco del timbro", "um pequeno ajuste de tom", "drobna korekta brzmienia", "een kleine toonaanpassing", "en liten tonjustering", "pieni sävykorjaus", "küçük bir ton düzeltmesi"],
    "the recording sounds fine as it is": ["salvestus kõlab hästi, parandust pole vaja", "запис звучить нормально, правка не потрібна", "die Aufnahme klingt gut, keine Korrektur nötig", "l'enregistrement sonne bien, aucune correction nécessaire", "la grabación suena bien, no hace falta corregir", "la registrazione suona bene, nessuna correzione necessaria", "a gravação soa bem, não precisa de correção", "nagranie brzmi dobrze, korekta niepotrzebna", "de opname klinkt prima, correctie niet nodig", "inspelningen låter bra, ingen korrigering behövs", "äänite kuulostaa hyvältä, korjausta ei tarvita", "kayıt iyi duyuluyor, düzeltme gerekmiyor"],
    "not connected to the output": ["väljundiga ühendamata", "не підключено до виходу", "nicht mit der Ausgabe verbunden", "non connecté à la sortie", "no conectado a la salida", "non collegato all'uscita", "não conectado à saída", "niepodłączone do wyjścia", "niet verbonden met de uitgang", "inte ansluten till utgången", "ei yhdistetty ulostuloon", "çıkışa bağlı değil"],
    "waiting for sound": ["ootab heli", "чекає на звук", "wartet auf Ton", "attend du son", "esperando sonido", "in attesa di audio", "aguardando som", "czeka na dźwięk", "wacht op geluid", "väntar på ljud", "odottaa ääntä", "ses bekliyor"],
    "working": ["töötab", "працює", "aktiv", "actif", "funcionando", "attivo", "funcionando", "działa", "actief", "aktiv", "toimii", "çalışıyor"],
    "Evenly is not connected to this output yet. Windows will ask for permission; sound stops for a second.": [
        "Evenly pole sellele väljundile veel ühendatud. Windows küsib luba ja heli katkeb sekundiks.",
        "Evenly ще не підключено до цього виходу. Windows запитає дозвіл, звук зникне на секунду.",
        "Evenly ist noch nicht mit dieser Ausgabe verbunden. Windows fragt nach der Erlaubnis, der Ton setzt eine Sekunde aus.",
        "Evenly n'est pas encore connecté à cette sortie. Windows demandera l'autorisation et le son sera coupé une seconde.",
        "Evenly aún no está conectado a esta salida. Windows pedirá permiso y el sonido se cortará un segundo.",
        "Evenly non è ancora collegato a questa uscita. Windows chiederà il permesso e l'audio si interromperà per un secondo.",
        "O Evenly ainda não está conectado a esta saída. O Windows pedirá permissão e o som some por um segundo.",
        "Evenly nie jest jeszcze podłączony do tego wyjścia. Windows poprosi o zgodę, dźwięk zniknie na sekundę.",
        "Evenly is nog niet verbonden met deze uitgang. Windows vraagt om toestemming; het geluid valt een seconde weg.",
        "Evenly är inte ansluten till den här utgången än. Windows ber om tillåtelse och ljudet försvinner i en sekund.",
        "Evenly ei ole vielä yhdistetty tähän ulostuloon. Windows kysyy luvan, ja ääni katkeaa sekunniksi.",
        "Evenly bu çıkışa henüz bağlı değil. Windows izin isteyecek, ses bir saniyeliğine kesilecek."],
    "Sound goes through FxSound now, so it would be processed twice. Switch the output to this device?": [
        "Heli läheb praegu läbi FxSoundi, nii töödeldaks seda kaks korda. Kas lülitada väljund sellele seadmele?",
        "Зараз звук іде через FxSound, тому його обробляли б двічі. Перемкнути вивід на цей пристрій?",
        "Der Ton läuft gerade über FxSound und würde doppelt verarbeitet. Ausgabe auf dieses Gerät umstellen?",
        "Le son passe actuellement par FxSound et serait donc traité deux fois. Basculer la sortie vers cet appareil ?",
        "El sonido pasa ahora por FxSound y se procesaría dos veces. ¿Cambiar la salida a este dispositivo?",
        "L'audio passa ora da FxSound e verrebbe elaborato due volte. Passare l'uscita a questo dispositivo?",
        "O som está passando pelo FxSound e seria processado duas vezes. Mudar a saída para este dispositivo?",
        "Dźwięk idzie teraz przez FxSound, więc byłby przetwarzany dwukrotnie. Przełączyć wyjście na to urządzenie?",
        "Het geluid loopt nu via FxSound en zou dubbel worden verwerkt. De uitgang naar dit apparaat omschakelen?",
        "Ljudet går nu via FxSound och skulle bearbetas två gånger. Byta utgång till den här enheten?",
        "Ääni kulkee nyt FxSoundin kautta ja käsiteltäisiin kahdesti. Vaihdetaanko ulostulo tähän laitteeseen?",
        "Ses şu anda FxSound üzerinden geçiyor, bu yüzden iki kez işlenirdi. Çıkış bu cihaza geçirilsin mi?"],
    "Untitled sound": ["Nimetu heli", "Звук без назви", "Ton ohne Titel", "Son sans titre", "Sonido sin título", "Audio senza titolo", "Som sem título", "Dźwięk bez tytułu", "Geluid zonder titel", "Ljud utan titel", "Nimetön ääni", "Başlıksız ses"],
    "Nothing is playing": ["Praegu ei mängi midagi", "Зараз нічого не грає", "Es läuft nichts", "Rien n'est en cours de lecture", "No se reproduce nada", "Non sta suonando nulla", "Nada está tocando", "Nic nie gra", "Er wordt niets afgespeeld", "Inget spelas", "Mitään ei soi", "Hiçbir şey çalmıyor"],
    "Play a video or music: the mode picks itself": ["Mängi videot või muusikat: režiim valitakse ise", "Увімкніть відео чи музику: режим підбереться сам", "Spiel ein Video oder Musik ab: der Modus wählt sich selbst", "Lancez une vidéo ou de la musique : le mode se choisit tout seul", "Reproduce un vídeo o música: el modo se elige solo", "Avvia un video o della musica: la modalità si sceglie da sola", "Reproduza um vídeo ou música: o modo se escolhe sozinho", "Włącz film lub muzykę: tryb wybierze się sam", "Speel een video of muziek af: de modus kiest zichzelf", "Spela en video eller musik: läget väljer sig självt", "Toista video tai musiikkia: tila valitaan itsestään", "Bir video veya müzik açın: mod kendiliğinden seçilir"],
    "no title from the app; the mode follows the sound": ["rakendus ei andnud pealkirja, režiim lähtub helist", "програма не повідомила назву, режим за звуком", "kein Titel von der App, der Modus richtet sich nach dem Klang", "pas de titre de l'application, le mode suit le son", "la aplicación no da título, el modo sigue al sonido", "nessun titolo dall'app, la modalità segue il suono", "o aplicativo não informou o título, o modo segue o som", "aplikacja nie podała tytułu, tryb wynika z dźwięku", "geen titel van de app, de modus volgt het geluid", "appen gav ingen titel, läget följer ljudet", "sovellus ei antanut otsikkoa, tila seuraa ääntä", "uygulama başlık vermedi, mod sese göre seçilir"],
    "Even loudness": ["Ühtlane valjus", "Рівна гучність", "Gleichmäßige Lautstärke", "Volume uniforme", "Volumen uniforme", "Volume uniforme", "Volume uniforme", "Równa głośność", "Gelijkmatig volume", "Jämn volym", "Tasainen äänenvoimakkuus", "Eşit ses düzeyi"],
    "input by frequency  ·  below: loudness over 90 seconds": ["sisend sageduste kaupa  ·  all: valjus 90 sekundi jooksul", "вхід за частотами  ·  нижче: гучність за 90 секунд", "Eingang nach Frequenz  ·  unten: Lautstärke der letzten 90 Sekunden", "entrée par fréquence  ·  en dessous : volume sur 90 secondes", "entrada por frecuencia  ·  abajo: volumen de los últimos 90 segundos", "ingresso per frequenza  ·  sotto: volume degli ultimi 90 secondi", "entrada por frequência  ·  abaixo: volume dos últimos 90 segundos", "wejście wg częstotliwości  ·  niżej: głośność z 90 sekund", "ingang per frequentie  ·  hieronder: volume van de laatste 90 seconden", "ingång per frekvens  ·  nedan: volym de senaste 90 sekunderna", "sisääntulo taajuuksittain  ·  alla: äänenvoimakkuus 90 sekunnin ajalta", "frekansa göre giriş  ·  altta: son 90 saniyenin ses düzeyi"],
    "it all comes alive with sound": ["kõik ärkab ellu, kui heli tuleb", "усе оживе, коли піде звук", "alles erwacht, sobald Ton kommt", "tout s'anime dès que le son démarre", "todo cobra vida cuando suena algo", "tutto si anima quando parte l'audio", "tudo ganha vida quando houver som", "wszystko ożyje, gdy pojawi się dźwięk", "alles komt tot leven zodra er geluid is", "allt vaknar när ljud kommer", "kaikki heräävät eloon, kun ääni alkaa", "ses gelince her şey canlanır"],
    "as it came": ["nagu tuli", "як надійшло", "wie eingehend", "tel que reçu", "como llegó", "come arriva", "como chegou", "jak wpłynęło", "zoals binnengekomen", "som det kom", "sellaisena kuin tuli", "geldiği gibi"],
    "as you hear it": ["nagu kuuled", "як ви чуєте", "wie du es hörst", "tel que vous l'entendez", "como lo oyes", "come lo senti", "como você ouve", "jak słyszysz", "zoals jij het hoort", "som du hör det", "sellaisena kuin kuulet", "duyduğun gibi"],
    "Volume": ["Helitugevus", "Гучність", "Lautstärke", "Volume", "Volumen", "Volume", "Volume", "Głośność", "Volume", "Volym", "Äänenvoimakkuus", "Ses"],
    "Sound: ": ["Heli: ", "Звук: ", "Klang: ", "Son : ", "Sonido: ", "Suono: ", "Som: ", "Dźwięk: ", "Geluid: ", "Ljud: ", "Ääni: ", "Ses: "],
    "Fix the tone of each recording": ["Paranda iga salvestuse tooni", "Виправляти тон кожного запису", "Klang jeder Aufnahme korrigieren", "Corriger le timbre de chaque enregistrement", "Corregir el tono de cada grabación", "Correggere il timbro di ogni registrazione", "Corrigir o tom de cada gravação", "Koryguj brzmienie każdego nagrania", "De toon van elke opname corrigeren", "Justera tonen i varje inspelning", "Korjaa jokaisen äänitteen sävy", "Her kaydın tonunu düzelt"],
    "Equalizer": ["Ekvalaiser", "Еквалайзер", "Equalizer", "Égaliseur", "Ecualizador", "Equalizzatore", "Equalizador", "Korektor", "Equalizer", "Equalizer", "Taajuuskorjain", "Ekolayzır"],
    "drag the points · double-click resets a point": ["lohista punkte · topeltklõps lähtestab punkti", "перетягуйте точки · подвійний клік скидає точку", "Punkte ziehen · Doppelklick setzt einen Punkt zurück", "faites glisser les points · double-clic pour réinitialiser un point", "arrastra los puntos · doble clic restablece un punto", "trascina i punti · doppio clic reimposta un punto", "arraste os pontos · clique duplo redefine um ponto", "przeciągaj punkty · dwuklik zeruje punkt", "sleep de punten · dubbelklik zet een punt terug", "dra punkterna · dubbelklicka återställer en punkt", "vedä pisteitä · kaksoisnapsautus nollaa pisteen", "noktaları sürükle · çift tıklama noktayı sıfırlar"],
    "10 bands, by hand": ["10 riba, käsitsi", "10 смуг, вручну", "10 Bänder, von Hand", "10 bandes, réglage manuel", "10 bandas, a mano", "10 bande, a mano", "10 bandas, manual", "10 pasm, ręcznie", "10 banden, handmatig", "10 band, för hand", "10 kaistaa, käsin", "10 bant, elle"],
    "Alt + ` switches the mode from any window": ["Alt + ` vahetab režiimi igast aknast", "Alt + ` змінює режим з будь-якого вікна", "Alt + ` wechselt den Modus aus jedem Fenster", "Alt + ` change le mode depuis n'importe quelle fenêtre", "Alt + ` cambia el modo desde cualquier ventana", "Alt + ` cambia modalità da qualsiasi finestra", "Alt + ` troca o modo em qualquer janela", "Alt + ` zmienia tryb z dowolnego okna", "Alt + ` wisselt de modus vanuit elk venster", "Alt + ` byter läge från vilket fönster som helst", "Alt + ` vaihtaa tilaa mistä tahansa ikkunasta", "Alt + ` modu her pencereden değiştirir"],
    "That did not work": ["See ei õnnestunud", "Не вийшло", "Das hat nicht geklappt", "Échec de l'opération", "No ha funcionado", "Non ha funzionato", "Não funcionou", "Nie udało się", "Dat is niet gelukt", "Det fungerade inte", "Se ei onnistunut", "Olmadı"],
    "Could not connect Evenly to the output: ": ["Evenlyt ei õnnestunud väljundiga ühendada: ", "Не вдалося підключити Evenly до виходу: ", "Evenly konnte nicht mit der Ausgabe verbunden werden: ", "Impossible de connecter Evenly à la sortie : ", "No se pudo conectar Evenly a la salida: ", "Impossibile collegare Evenly all'uscita: ", "Não foi possível conectar o Evenly à saída: ", "Nie udało się podłączyć Evenly do wyjścia: ", "Evenly kon niet met de uitgang worden verbonden: ", "Det gick inte att ansluta Evenly till utgången: ", "Evenlyä ei voitu yhdistää ulostuloon: ", "Evenly çıkışa bağlanamadı: "],
    "For cut-off words after a pause; may hiss slightly": ["Kui pärast pausi lõppevad sõnad ära; võib kergelt sisiseda", "Якщо після паузи губляться слова; може трохи шипіти", "Wenn nach einer Pause Wörter fehlen; kann leicht rauschen", "Si des mots sont coupés après une pause ; léger souffle possible", "Si se pierden palabras tras una pausa; puede sisear un poco", "Se dopo una pausa le parole si perdono; può fruscare un po'", "Se palavras são cortadas após uma pausa; pode chiar um pouco", "Gdy po przerwie giną słowa; może lekko szumieć", "Als woorden na een pauze wegvallen; kan licht sissen", "Om ord försvinner efter en paus; kan väsa lite", "Jos sanat katkeavat tauon jälkeen; voi suhista hieman", "Duraklamadan sonra sözcükler kesiliyorsa; hafif hışırdayabilir"],
    "Keep the output awake": ["Ära lase väljundil magama jääda", "Не давати виходу засинати", "Ausgabe wach halten", "Empêcher la sortie de s'endormir", "Mantener la salida despierta", "Mantieni sveglia l'uscita", "Manter a saída acordada", "Nie pozwalaj wyjściu zasypiać", "Uitgang wakker houden", "Håll utgången vaken", "Pidä ulostulo hereillä", "Çıkışın uykuya dalmasını engelle"],
    "  ·  holding": ["  ·  hoiab", "  ·  тримає", "  ·  hält", "  ·  actif", "  ·  activo", "  ·  attivo", "  ·  mantendo", "  ·  trzyma", "  ·  houdt vast", "  ·  håller", "  ·  pitää", "  ·  tutuyor"],
    "  ·  output unavailable, waiting": ["  ·  väljund pole saadaval, ootan", "  ·  вихід недоступний, чекаю", "  ·  Ausgabe nicht verfügbar, warte", "  ·  sortie indisponible, en attente", "  ·  salida no disponible, esperando", "  ·  uscita non disponibile, attendo", "  ·  saída indisponível, aguardando", "  ·  wyjście niedostępne, czekam", "  ·  uitgang niet beschikbaar, wachten", "  ·  utgången är inte tillgänglig, väntar", "  ·  ulostulo ei käytettävissä, odotan", "  ·  çıkış kullanılamıyor, bekleniyor"],
    "  ·  failed": ["  ·  ebaõnnestus", "  ·  не вийшло", "  ·  fehlgeschlagen", "  ·  échec", "  ·  falló", "  ·  non riuscito", "  ·  falhou", "  ·  nie udało się", "  ·  mislukt", "  ·  misslyckades", "  ·  epäonnistui", "  ·  başarısız"],
    "FxSound-style loudness (no leveling)": ["FxSoundi stiilis valjus (ilma ühtlustamiseta)", "Гучність як у FxSound (без вирівнювання)", "Lautstärke wie bei FxSound (ohne Angleichung)", "Volume façon FxSound (sans nivellement)", "Volumen al estilo FxSound (sin nivelación)", "Volume in stile FxSound (senza livellamento)", "Volume no estilo FxSound (sem nivelamento)", "Głośność jak w FxSound (bez wyrównywania)", "Volume zoals FxSound (zonder nivellering)", "Volym som i FxSound (utan utjämning)", "Äänenvoimakkuus kuten FxSoundissa (ilman tasausta)", "FxSound tarzı ses düzeyi (eşitleme yok)"],
    "Automatic (by monitor size)": ["Automaatne (monitori suuruse järgi)", "Авто (за розміром монітора)", "Automatisch (nach Monitorgröße)", "Automatique (selon la taille de l'écran)", "Automático (según el tamaño del monitor)", "Automatico (in base alle dimensioni del monitor)", "Automático (pelo tamanho do monitor)", "Automatycznie (według rozmiaru monitora)", "Automatisch (op basis van monitorgrootte)", "Automatiskt (efter skärmstorlek)", "Automaattinen (näytön koon mukaan)", "Otomatik (monitör boyutuna göre)"],
    "Window size": ["Akna suurus", "Розмір вікна", "Fenstergröße", "Taille de la fenêtre", "Tamaño de la ventana", "Dimensione della finestra", "Tamanho da janela", "Rozmiar okna", "Venstergrootte", "Fönsterstorlek", "Ikkunan koko", "Pencere boyutu"],
    "Forget what the app has learned": ["Unusta, mida rakendus on õppinud", "Забути, чого навчилася програма", "Gelerntes vergessen", "Oublier ce que l'application a appris", "Olvidar lo que ha aprendido la aplicación", "Dimentica ciò che l'app ha imparato", "Esquecer o que o aplicativo aprendeu", "Zapomnij, czego nauczyła się aplikacja", "Vergeet wat de app heeft geleerd", "Glöm vad appen har lärt sig", "Unohda, mitä sovellus on oppinut", "Uygulamanın öğrendiklerini unut"],
    "Disconnect Evenly from this output": ["Ühenda Evenly sellest väljundist lahti", "Відключити Evenly від цього виходу", "Evenly von dieser Ausgabe trennen", "Déconnecter Evenly de cette sortie", "Desconectar Evenly de esta salida", "Scollega Evenly da questa uscita", "Desconectar o Evenly desta saída", "Odłącz Evenly od tego wyjścia", "Evenly loskoppelen van deze uitgang", "Koppla bort Evenly från den här utgången", "Irrota Evenly tästä ulostulosta", "Evenly'yi bu çıkıştan ayır"],
    "Sound mode": ["Heli režiim", "Режим звуку", "Klangmodus", "Mode sonore", "Modo de sonido", "Modalità audio", "Modo de som", "Tryb dźwięku", "Geluidsmodus", "Ljudläge", "Äänitila", "Ses modu"],
    "auto now: ": ["praegu auto: ", "зараз авто: ", "jetzt automatisch: ", "auto actuellement : ", "ahora auto: ", "ora auto: ", "agora auto: ", "teraz auto: ", "nu auto: ", "nu auto: ", "nyt auto: ", "şimdi otomatik: "],
    "picked by hand": ["valitud käsitsi", "обрано вручну", "von Hand gewählt", "choisi manuellement", "elegido a mano", "scelto a mano", "escolhido manualmente", "wybrane ręcznie", "handmatig gekozen", "valt för hand", "valittu käsin", "elle seçildi"],
    "Music": ["Muusika", "Музика", "Musik", "Musique", "Música", "Musica", "Música", "Muzyka", "Muziek", "Musik", "Musiikki", "Müzik"],
    "Speech": ["Kõne", "Мова", "Sprache", "Voix", "Voz", "Voce", "Fala", "Mowa", "Spraak", "Tal", "Puhe", "Konuşma"],
    "Movies": ["Filmid", "Кіно", "Filme", "Films", "Cine", "Film", "Filmes", "Filmy", "Films", "Film", "Elokuvat", "Filmler"],
    "Games": ["Mängud", "Ігри", "Spiele", "Jeux", "Juegos", "Giochi", "Jogos", "Gry", "Games", "Spel", "Pelit", "Oyunlar"],
    "Auto": ["Auto", "Авто", "Auto", "Auto", "Auto", "Auto", "Auto", "Auto", "Auto", "Auto", "Auto", "Otomatik"],
    "Clarity": ["Selgus", "Чіткість", "Klarheit", "Clarté", "Claridad", "Chiarezza", "Clareza", "Klarowność", "Helderheid", "Klarhet", "Selkeys", "Netlik"],
    "Bass": ["Bass", "Бас", "Bass", "Basses", "Graves", "Bassi", "Graves", "Bas", "Bas", "Bas", "Basso", "Bas"],
    "Width": ["Ruumilisus", "Об’єм", "Breite", "Largeur", "Amplitud", "Ampiezza", "Largura", "Przestrzeń", "Breedte", "Bredd", "Leveys", "Genişlik"],
    "Ambience": ["Atmosfäär", "Атмосфера", "Atmosphäre", "Ambiance", "Ambiente", "Ambiente", "Ambiente", "Atmosfera", "Sfeer", "Atmosfär", "Tunnelma", "Ortam"],
    "Language": ["Keel", "Мова", "Sprache", "Langue", "Idioma", "Lingua", "Idioma", "Język", "Taal", "Språk", "Kieli", "Dil"],
    "Automatic": ["Automaatne", "Авто", "Automatisch", "Automatique", "Automático", "Automatico", "Automático", "Automatycznie", "Automatisch", "Automatiskt", "Automaattinen", "Otomatik"],
}


def cpp_string(s):
    out = s.replace("\\", "\\\\").replace('"', '\\"')
    # keep non-ASCII as UTF-8 bytes: the app is compiled with /utf-8
    return '"' + out + '"'


def main():
    used = {}
    for _, _, en in pairs():
        used[en] = True
    missing = [k for k in used if k not in T]
    unused = [k for k in T if k not in used]
    bad = [k for k, v in T.items() if len(v) != len(ORDER)]
    problems = []
    for k, v in T.items():
        if len(v) != len(ORDER):
            continue
        for lang, text in zip(ORDER, v):
            if not text.strip():
                problems.append(f"{k!r} [{lang}] is empty")
            # spaces around fragments are part of the string (they are glued to other text)
            if k != k.lstrip() and text[: len(k) - len(k.lstrip())] != k[: len(k) - len(k.lstrip())]:
                problems.append(f"{k!r} [{lang}] lost its leading spaces")
            if k != k.rstrip() and text[len(text.rstrip()):] != k[len(k.rstrip()):]:
                problems.append(f"{k!r} [{lang}] lost its trailing spaces")
    if missing or bad or problems:
        for k in missing:
            print("MISSING:", k)
        for k in bad:
            print("WRONG COUNT:", k)
        for p in problems:
            print("PROBLEM:", p)
        sys.exit(1)
    if unused:
        print("note: not used by the app any more:", unused)
    out = pathlib.Path(__file__).resolve().parent.parent / "Source" / "app" / "Translations.inc"
    lines = ["// Generated by tools/translations.py. Columns: English, " + ", ".join(ORDER) + ".",
             "static const char* const kTranslations[][" + str(len(ORDER) + 1) + "] = {"]
    for k in used:
        lines.append("    { " + ", ".join(cpp_string(x) for x in [k] + T[k]) + " },")
    lines.append("};")
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"{len(used)} strings x {len(ORDER)} languages written to {out}")


if __name__ == "__main__":
    main()
