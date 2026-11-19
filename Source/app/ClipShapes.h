#pragma once
// Drawings made of ONE line, for the "Clip" picture (after the video for "Do I Wanna Know?", where the sound wave turns into line
// drawings). Each one is a single stroke in a 100 x 100 box (x to the right, y DOWN), entered from its first point and left from its
// last; the picture's line comes in from the left edge of the screen, draws it and goes on to the right edge.
// "words": the words of a ~song that ~call for this drawing, as the start of a word (English and Russian), separated by spaces.
// Paths use absolute M L C Q only; "@name" is a drawing made by code (see ClipLine::generated).
// The previews are drawn by tools/preview_clip_shapes.py from this same file: keep one shape per line.

namespace rv::ui
{
struct ClipShape { const char* name; const char* words; const char* path; };

inline const ClipShape kClipShapes[] = {
{ "heart", "hurt pain broken break ache feel feeling ~miss lonely боль болит разбит скуч одинок чувств heart ~love lov сердц ~любл ~любов ~люб", "M 50 90 C 30 75 8 58 8 36 C 8 18 22 8 35 10 C 44 11 50 20 50 28 C 50 20 56 11 65 10 C 78 8 92 18 92 36 C 92 58 70 75 50 90" },
{ "moon", "tired sleep asleep bed ~lying устал кровать лежу moon ~night dream sleep midnight ~dark луна лун ~ночь ночн сон сны снил сплю спать темн", "M 62 12 C 35 14 18 32 18 52 C 18 74 36 90 58 90 C 72 90 82 84 88 76 C 80 79 74 80 68 80 C 50 80 36 66 36 48 C 36 32 46 18 62 12" },
{ "star", "star stars shine sky звезд неб сия", "M 10 39 L 90 39 L 25 86 L 50 10 L 75 86 L 10 39" },
{ "glass", "drink drinks drunk whisk wine beer glass bottle bar cheers spill пью пил пьян вин пив бокал стакан рюмк бутыл бар", "M 30 92 L 50 90 L 50 58 C 36 56 28 42 30 14 L 70 14 C 72 42 64 56 50 58 L 50 90 L 70 92" },
{ "phone", "~call calling phone ~ring ~text ~message dial звон звоню телефон смс сообщен", "M 50 92 L 40 92 C 35 92 32 89 32 84 L 32 16 C 32 11 35 8 40 8 L 60 8 C 65 8 68 11 68 16 L 68 84 C 68 89 65 92 60 92 L 50 92 L 50 87 C 47 87 47 82 50 82 C 53 82 53 87 50 87" },
{ "face", "face cheek cheeks ~colour ~color ~smile blush лиц щек щёк улыб румян", "M 32 96 C 33 84 31 74 28 66 C 21 60 19 50 21 40 C 24 22 38 10 55 10 C 70 10 80 20 80 32 C 80 38 84 43 89 49 C 86 51 83 51 80 52 C 82 55 81 57 78 58 C 80 60 79 63 77 64 C 79 68 77 72 72 72 C 66 72 62 75 62 81 L 64 96" },
{ "eye", "~face ~watch ~seen ~find eye eyes ~see ~look watch stare глаз гляд ~смотр ~вижу ~видел взгляд", "M 4 50 C 25 24 75 24 96 50 C 75 76 25 76 4 50 L 37 50 C 37 33 63 33 63 50 C 63 67 37 67 37 50 L 96 50" },
{ "lips", "pucker taste ~say ~said ~talk ~tell ~told advice words ~word lie lies liar вкус ~скаж ~говор слов ложь врёшь lips mouth kiss kissed secret secrets tongue whisper губ поцел рот шепч тайн секрет", "M 4 50 C 18 40 34 30 45 38 C 48 41 52 41 55 38 C 66 30 82 40 96 50 C 80 56 66 70 50 70 C 34 70 20 56 4 50 C 30 53 70 53 96 50" },
{ "car", "car cars drive driving road race racing wheel engine машин еду ехал гонк дорог руль", "M 2 70 L 9 70 C 9 62 21 62 21 70 C 21 78 9 78 9 70 C 9 62 21 62 21 70 L 71 70 C 71 62 83 62 83 70 C 83 78 71 78 71 70 C 71 62 83 62 83 70 L 96 70 L 96 58 C 96 54 92 52 86 51 L 70 48 C 64 40 58 36 50 36 L 32 36 C 26 36 22 42 18 48 L 6 52 C 3 54 2 58 2 64" },
{ "tyre", "tyre tire wheel ~round ~turn spin колес шин круг кружит", "M 5 50 C 5 25 25 5 50 5 C 75 5 95 25 95 50 C 95 75 75 95 50 95 C 25 95 5 75 5 50 L 38 50 C 38 43 43 38 50 38 C 57 38 62 43 62 50 C 62 57 57 62 50 62 C 43 62 38 57 38 50" },
{ "flag", "flag ~win winner finish victory флаг финиш побед", "M 20 96 L 20 10 C 35 4 50 16 65 10 C 75 6 85 8 90 10 L 90 50 C 80 46 72 48 65 50 C 50 56 35 44 20 50 L 20 30 C 35 24 50 36 65 30 C 75 26 85 28 90 30" },
{ "woman", "stay lying girl girls woman women lady ~she ~her ~baby ~darling dance dancing девуш девочк женщ ~она ~её ~ее малыш детк танц", "M 6 92 L 20 58 C 17 52 18 46 23 43 C 16 40 14 30 22 24 C 30 18 40 22 40 32 C 40 37 37 40 33 41 C 35 46 39 49 42 52 C 47 50 50 54 47 59 C 47 66 51 73 57 75 C 61 66 65 58 70 52 C 73 51 75 53 75 56 C 78 66 82 78 86 90 L 96 90" },
{ "house", "hall wall walls yard window стен окн двор ~home house door room return ~дом домой двер комнат ~верн", "M 10 90 L 10 45 L 50 10 L 90 45 L 90 90 L 60 90 L 60 64 L 40 64 L 40 90 L 10 90" },
{ "note", "voice ~hear heard echo echoing stereo sound sounds голос слыш эхо звук ~song songs tune ~sing singing ~music melody ~record ~radio песн мелод музык ~пою поёт ~пел радио", "M 30 80 C 30 70 45 68 48 76 C 50 84 36 90 30 80 C 36 78 42 78 48 76 L 48 16 L 76 10 L 76 66 C 76 56 61 56 59 64 C 57 72 71 77 76 66" },
{ "waves", "sea ocean wave waves tide ~deep ~water swim drown river мор океан волн глуб вод плыв тон рек", "M 0 62 C 10 40 25 35 32 45 C 26 45 24 52 30 55 C 40 60 45 40 55 38 C 65 36 72 48 66 52 C 72 56 80 45 85 42 C 92 38 100 50 100 52" },
{ "flame", "fire burn burning flame ~hot heat smoke cigarette огон огн гор горит пламя жар дым сигарет", "M 50 95 C 25 95 15 75 25 55 C 28 65 34 68 36 66 C 30 50 38 30 55 8 C 55 25 70 35 75 50 C 78 45 78 40 76 35 C 90 55 85 95 50 95" },
{ "cloud", "grieve grief sad sorrow ~blue rain cloud clouds storm cry crying tears grey дожд туч облак гроз плач слез слёз", "M 20 60 C 8 60 5 45 18 40 C 18 25 38 20 45 30 C 52 15 78 18 78 35 C 92 35 95 58 80 60 L 20 60 L 26 76 L 32 62 L 40 80 L 46 62 L 54 80 L 60 62 L 68 78" },
{ "sun", "sun sunshine ~light morning ~day bright summer солн ~свет утр ~ден ~лет", "@sun" },
{ "clock", "~age ~old ~young ~forever ~again ~still вечн ~снова ~time clock hour hours minute ~late wait waiting ~tonight ~врем ~час минут поздн жду ждать", "M 50 5 C 75 5 95 25 95 50 C 95 75 75 95 50 95 C 25 95 5 75 5 50 C 5 25 25 5 50 5 L 50 50 L 72 62" },
{ "bird", "fly flying bird birds wing wings ~free ~away птиц крыл ~лет улет свобод", "M 4 58 C 18 36 34 34 48 56 C 60 34 80 30 96 50" },
{ "card", "card cards ace aces sleeve ~game ~play ~luck bet карт туз игр игра удач ставк", "M 25 50 L 25 10 L 75 10 L 75 90 L 25 90 L 25 50 L 38 50 L 50 30 L 62 50 L 50 70 L 38 50 L 75 50" },
{ "crown", "king queen crown gold rich royal корол корон цар золот", "M 10 82 L 10 30 L 30 56 L 50 18 L 70 56 L 90 30 L 90 82 L 10 82" },
{ "flower", "rose roses flower flowers bloom spring garden роз цвет весн сад", "@flower" },
{ "gem", "diamond diamonds money cash ~ring rings jewel бриллиант алмаз деньг кольц", "M 10 36 L 30 15 L 70 15 L 90 36 L 50 90 L 10 36 L 90 36" },
{ "key", "key keys lock ~open secret ключ замок откр", "M 5 50 C 5 36 30 36 30 50 C 30 64 5 64 5 50 L 30 50 L 95 50 L 95 62 L 88 62 L 88 50 L 80 50 L 80 60 L 74 60 L 74 50" },
{ "question", "~know ~why ~what wonder wondering question ~maybe answer ~знаю ~знать почему зачем вопрос ~может ответ", "M 30 32 C 30 10 70 10 70 32 C 70 46 50 46 50 62 L 50 72 C 46 74 46 82 50 84 C 54 82 54 74 50 72" },
{ "spiral", "brain idea ideals ~crazy strange stupid insane ~hide hiding dizzy ~mind ~head crazy lost dizzy ~think thinking ~thought ум ~голов сума потерял кружит ~думаю ~думал мысл", "@spiral" },
// the video's chorus: legs in boots walking along the line ("crawlin' back to you")
{ "legs", "crawl walk walking run running ~stand step steps feet foot boots legs leg heels ~back ground иду идти шаг бег ног сапог каблук ~назад земл стою", "M 2 96 L 14 96 C 14 90 20 88 26 86 L 30 60 L 36 30 C 40 14 60 14 64 30 L 70 60 L 74 86 C 80 88 86 90 88 96 L 98 96" },
{ "dancer", "~fight fist accused драк кулак dance dancing party ~move ~body ~tonight танц вечерин двиг тело", "M 30 96 L 46 62 L 62 96 L 46 62 L 46 36 L 24 20 L 46 36 L 70 18 L 46 36 L 46 30 C 39 30 37 20 46 15 C 55 15 57 27 46 30" },
{ "am", "", "M 5 85 L 25 15 L 45 85 L 55 85 L 55 15 L 75 55 L 95 15 L 95 85" },
};

// Capital letters, each one stroke (going back over a stroke where needed), in the same 100 x 100 box: the end of a song spells the
// initials of the artist, as the video ends on "AM".
static const char* const kClipLetters[26] = {
    "M 10 95 L 30 50 L 70 50 L 30 50 L 50 5 L 90 95",                                                     // A
    "M 15 95 L 15 5 L 60 5 C 85 5 85 48 60 48 L 15 48 L 65 48 C 92 48 92 95 65 95 L 15 95",       // B
    "M 85 80 C 70 100 15 100 12 50 C 15 0 70 0 85 20",                                            // C
    "M 15 95 L 15 5 L 50 5 C 95 5 95 95 50 95 L 15 95",                                           // D
    "M 85 95 L 15 95 L 15 50 L 70 50 L 15 50 L 15 5 L 85 5",                                      // E
    "M 15 95 L 15 5 L 85 5 L 15 5 L 15 50 L 70 50",                                              // F
    "M 88 95 L 88 60 L 55 60 L 88 60 C 80 100 15 100 12 50 C 15 0 70 0 85 20",                                    // G
    "M 15 5 L 15 95 L 15 50 L 85 50 L 85 5 L 85 95",                                              // H
    "M 30 95 L 70 95 L 50 95 L 50 5 L 30 5 L 70 5",                                               // I
    "M 18 70 C 20 100 65 100 65 70 L 65 5 L 30 5 L 80 5",                                         // J
    "M 15 5 L 15 95 L 15 55 L 85 5 L 40 37 L 85 95",                                              // K
    "M 15 5 L 15 95 L 85 95",                                                                     // L
    "M 10 95 L 15 5 L 50 60 L 85 5 L 90 95",                                                      // M
    "M 15 95 L 15 5 L 85 95 L 85 5",                                                              // N
    "M 50 95 C 5 95 5 5 50 5 C 95 5 95 95 50 95",                                                  // O
    "M 15 95 L 15 5 L 60 5 C 92 5 92 52 60 52 L 15 52",                                           // P
    "M 62 72 L 90 100 L 76 86 C 62 98 6 96 8 50 C 6 4 94 4 92 50 C 92 70 86 80 76 86",            // Q
    "M 15 95 L 15 5 L 60 5 C 92 5 92 52 60 52 L 15 52 L 40 52 L 85 95",                           // R
    "M 12 85 C 30 100 85 100 85 72 C 85 45 15 50 15 28 C 15 0 65 0 85 15",                        // S
    "M 50 95 L 50 5 L 10 5 L 90 5",                                                               // T
    "M 15 5 L 15 65 C 15 100 85 100 85 65 L 85 5",                                                // U
    "M 10 5 L 50 95 L 90 5",                                                                      // V
    "M 5 5 L 25 95 L 50 35 L 75 95 L 95 5",                                                       // W
    "M 10 5 L 90 95 L 50 50 L 90 5 L 10 95",                                                      // X
    "M 10 5 L 50 50 L 90 5 L 50 50 L 50 95",                                                      // Y
    "M 90 95 L 10 95 L 90 5 L 10 5",                                                              // Z
};} // namespace rv::ui
