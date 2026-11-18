// pi_core/config.cpp — see config.hpp
#include "config.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace pi_core {

namespace {

void fail(const std::string& what) {
    std::fprintf(stderr, "Ошибка аргументов: %s\n", what.c_str());
    print_usage();
    std::exit(2);
}

bool parse_size_str(const std::string& text, int& w, int& h) {
    // "640x360" (spaces allowed, same as Python parse_size)
    std::string t;
    t.reserve(text.size());
    for (char c : text)
        if (c != ' ') t.push_back(static_cast<char>(std::tolower((unsigned char)c)));
    const auto x = t.find('x');
    if (x == std::string::npos || x == 0 || x + 1 == t.size()) return false;
    const int wv = std::atoi(t.substr(0, x).c_str());
    const int hv = std::atoi(t.substr(x + 1).c_str());
    if (wv <= 0 || hv <= 0) return false;
    w = wv;
    h = hv;
    return true;
}

bool parse_tolerance_str(const std::string& text, int& th, int& ts, int& tv) {
    // "40,100,100"
    int vals[3];
    size_t pos = 0;
    for (int i = 0; i < 3; ++i) {
        const size_t comma = text.find(',', pos);
        const std::string part =
            i == 2 ? text.substr(pos) : text.substr(pos, comma - pos);
        if (part.empty()) return false;
        size_t idx = 0;
        const int v = std::stoi(part, &idx);
        if (idx != part.size()) return false;
        vals[i] = v;
        if (i < 2) {
            if (comma == std::string::npos) return false;
            pos = comma + 1;
        }
    }
    th = vals[0];
    ts = vals[1];
    tv = vals[2];
    return true;
}

int parse_int(const char* s, const char* flag) {
    if (!s) fail(std::string(flag) + ": missing value");
    char* end = nullptr;
    const long v = std::strtol(s, &end, 10);
    if (end == s || *end != '\0') fail(std::string(flag) + ": ожидалось целое");
    return static_cast<int>(v);
}

double parse_double(const char* s, const char* flag) {
    if (!s) fail(std::string(flag) + ": missing value");
    char* end = nullptr;
    const double v = std::strtod(s, &end);
    if (end == s || *end != '\0') fail(std::string(flag) + ": ожидалось число");
    return v;
}

const char* next_arg(int argc, char** argv, int& i, const char* flag) {
    if (i + 1 >= argc) fail(std::string(flag) + ": missing value");
    return argv[++i];
}

}  // namespace

void print_usage() {
    std::fprintf(stderr,
        "pi_core — C++ замена rpi_elp_cam.py (ELP 180°, Raspberry Pi 4)\n"
        "\n"
        "Ключи (по умолчанию):\n"
        "  --device ПУТЬ            /dev/video0\n"
        "  --capture-size ШxВ       1920x1080 (единственный режим с полным FOV)\n"
        "  --track-size ШxВ         640x360\n"
        "  --fps N                  30 (запрошенный FPS камеры)\n"
        "  --stream-size ШxВ        = track-size\n"
        "  --stream-fps N           26\n"
        "  --jpeg-quality N         55\n"
        "  --no-stream              не слать видео до {\"stream\": true}\n"
        "  --laptop-ip IP           10.192.226.77 (меняется вместе с сетью!)\n"
        "  --video-port N           5555\n"
        "  --rgb-port N             5557\n"
        "  --serial ПУТЬ            /dev/serial0\n"
        "  --no-serial              не открывать Serial\n"
        "  --print-serial           serial-строки в stdout вместо устройства\n"
        "                           (дымовая проверка без Arduino; перекрывает\n"
        "                           --no-serial)\n"
        "  --exposure N             300 (не опускать ниже ~260: падает FPS)\n"
        "  --gain N                 0\n"
        "  --brightness N           0 (64 вымывает цвета, S 168 -> 20)\n"
        "  --gamma N                100 (72..500)\n"
        "  --saturation N           60 (0..128)\n"
        "  --contrast N             32 (0..64)\n"
        "  --power-line N           1 (0=выкл, 1=50 Гц, 2=60 Гц)\n"
        "  --tolerance H,S,V        40,100,100\n"
        "  --gates N                2 (у вас 3 цвета — ставьте 3)\n"
        "  --merge-gap PX           0 = 2.5%% ширины кадра (16 px при 640);\n"
        "                           куски ворот с зазором <= PX сливаются в\n"
        "                           одни ворота («тело противника»)\n"
        "  --min-area PX            0 = 100 px в эквиваленте 1080p;\n"
        "                           куски меньше — не ворота\n"
        "  --max-stray PX           0 = 10%% ширины кадра (64 px при 640);\n"
        "                           дальше от основного кластера — чужой\n"
        "                           объект («майка»); то же значение —\n"
        "                           радиус непрерывности трека\n"
        "  --lost-frames N          5; ворота теряются только после N пустых\n"
        "                           кадров подряд — тогда G<id>,-1,0 (один раз)\n"
        "  --confirm-frames N       3; кадров подряд до принятия нового\n"
        "                           обнаружения\n"
        "  --open-kernel N          3 (нечётное, 1 = выкл); морфология OPEN\n"
        "                           после inRange\n"
        "  --self-cx PX             0 = центр кадра; круг self-маски X\n"
        "                           (Idea 8: корпус робота вырезается из\n"
        "                           поиска цвета до морфологии/кластеризации)\n"
        "  --self-cy PX             0 = центр кадра; круг self-маски Y\n"
        "  --self-r PX              0 = 0.22*min(ширина,высота) ≈ 79 px при\n"
        "                           640x360; внутренний радиус self-маски\n"
        "  --self-r2 PX             0 = сплошной круг; внешний радиус кольца\n"
        "                           (справочно: в референсе кольцо\n"
        "                           r 110..355 px)\n"
        "  --no-self-mask           выключить self-маску (корпус НЕ\n"
        "                           исключается из поиска)\n"
        "  --dist-calib ПУТЬ        /home/twox/distance_calib.json;\n"
        "                           калибровка дистанции d = a/(b-r) - c\n"
        "                           (Idea 6, пишет out/dist_calibrate.py);\n"
        "                           нет файла/битый — аналитическая модель\n"
        "                           Z = ширина_ворот * (ширина/π) / w_px\n"
        "  --dist-hold СЕК          2.0; сколько секунд держать последнее\n"
        "                           достоверное значение дистанции, когда\n"
        "                           ворота заслонены (0 = не держать)\n"
        "  --dist-drop FRAC         0.6; радиус упал ниже FRAC*эталона при\n"
        "                           неподвижном центроиде -> дистанция\n"
        "                           недостоверна (0 = выкл)\n"
        "  --dist-fill FRAC         0.25; мин. заполненность бокса\n"
        "                           area/(w*h), ниже — недостоверна (0 = выкл)\n"
        "  --gate-width СМ          45 (регламент 45-60); ширина ворот для\n"
        "                           аналитической модели\n"
        "  --no-dist                не считать дистанцию (статус без dist_cm)\n"
        "  --rotate N               0 (0/90/180/270)\n"
        "  --threads N              0 (авто = 1; N>1 замедляет декод втрое)\n"
        "  --duration СЕК           0 (0 = бесконечно)\n"
        "  --gates-file ПУТЬ        /home/twox/saved_gates.json\n"
        "  --probe                  проверить камеру и выйти\n"
        "  --snapshot ФАЙЛ          сохранить кадр (и *_full) и выйти\n"
        "  --verbose                подробный лог\n");
}

Config parse_args(int argc, char** argv) {
    Config cfg;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--device") {
            cfg.device = next_arg(argc, argv, i, a.c_str());
        } else if (a == "--capture-size") {
            const std::string v = next_arg(argc, argv, i, a.c_str());
            if (!parse_size_str(v, cfg.capture_w, cfg.capture_h))
                fail(a + ": размер должен быть ШИРИНАxВЫСОТА");
        } else if (a == "--track-size") {
            const std::string v = next_arg(argc, argv, i, a.c_str());
            if (!parse_size_str(v, cfg.track_w, cfg.track_h))
                fail(a + ": размер должен быть ШИРИНАxВЫСОТА");
        } else if (a == "--fps") {
            cfg.camera_fps = parse_int(next_arg(argc, argv, i, a.c_str()), a.c_str());
        } else if (a == "--stream-size") {
            const std::string v = next_arg(argc, argv, i, a.c_str());
            if (!parse_size_str(v, cfg.stream_w, cfg.stream_h))
                fail(a + ": размер должен быть ШИРИНАxВЫСОТА");
            cfg.stream_size_set = true;
        } else if (a == "--stream-fps") {
            cfg.stream_fps = parse_int(next_arg(argc, argv, i, a.c_str()), a.c_str());
        } else if (a == "--jpeg-quality") {
            cfg.jpeg_quality = parse_int(next_arg(argc, argv, i, a.c_str()), a.c_str());
        } else if (a == "--no-stream") {
            cfg.stream_on_start = false;
        } else if (a == "--laptop-ip") {
            cfg.laptop_ip = next_arg(argc, argv, i, a.c_str());
        } else if (a == "--video-port") {
            cfg.video_port = parse_int(next_arg(argc, argv, i, a.c_str()), a.c_str());
        } else if (a == "--rgb-port") {
            cfg.rgb_port = parse_int(next_arg(argc, argv, i, a.c_str()), a.c_str());
        } else if (a == "--serial") {
            cfg.serial_port = next_arg(argc, argv, i, a.c_str());
            cfg.serial_enabled = true;
        } else if (a == "--no-serial") {
            cfg.serial_enabled = false;
        } else if (a == "--print-serial") {
            cfg.print_serial = true;
            cfg.serial_enabled = true;  // overrides --no-serial
        } else if (a == "--exposure") {
            cfg.exposure = parse_int(next_arg(argc, argv, i, a.c_str()), a.c_str());
        } else if (a == "--gain") {
            cfg.gain = parse_int(next_arg(argc, argv, i, a.c_str()), a.c_str());
        } else if (a == "--brightness") {
            cfg.brightness = parse_int(next_arg(argc, argv, i, a.c_str()), a.c_str());
        } else if (a == "--gamma") {
            cfg.gamma = parse_int(next_arg(argc, argv, i, a.c_str()), a.c_str());
        } else if (a == "--saturation") {
            cfg.saturation = parse_int(next_arg(argc, argv, i, a.c_str()), a.c_str());
        } else if (a == "--contrast") {
            cfg.contrast = parse_int(next_arg(argc, argv, i, a.c_str()), a.c_str());
        } else if (a == "--power-line") {
            const int v = parse_int(next_arg(argc, argv, i, a.c_str()), a.c_str());
            if (v < 0 || v > 2) fail(a + ": ожидалось 0, 1 или 2");
            cfg.power_line = v;
        } else if (a == "--tolerance") {
            const std::string v = next_arg(argc, argv, i, a.c_str());
            if (!parse_tolerance_str(v, cfg.tol_h, cfg.tol_s, cfg.tol_v))
                fail(a + ": допуск должен быть H,S,V");
        } else if (a == "--gates") {
            const int v = parse_int(next_arg(argc, argv, i, a.c_str()), a.c_str());
            if (v < 1) fail(a + ": минимум 1");
            cfg.gates = v;
        } else if (a == "--merge-gap") {
            cfg.merge_gap = parse_double(next_arg(argc, argv, i, a.c_str()), a.c_str());
        } else if (a == "--min-area") {
            cfg.min_area = parse_double(next_arg(argc, argv, i, a.c_str()), a.c_str());
        } else if (a == "--max-stray") {
            cfg.max_stray = parse_double(next_arg(argc, argv, i, a.c_str()), a.c_str());
        } else if (a == "--lost-frames") {
            cfg.lost_frames = parse_int(next_arg(argc, argv, i, a.c_str()), a.c_str());
        } else if (a == "--confirm-frames") {
            cfg.confirm_frames = parse_int(next_arg(argc, argv, i, a.c_str()), a.c_str());
        } else if (a == "--open-kernel") {
            cfg.open_kernel = parse_int(next_arg(argc, argv, i, a.c_str()), a.c_str());
        } else if (a == "--self-cx") {
            cfg.self_cx = parse_double(next_arg(argc, argv, i, a.c_str()), a.c_str());
        } else if (a == "--self-cy") {
            cfg.self_cy = parse_double(next_arg(argc, argv, i, a.c_str()), a.c_str());
        } else if (a == "--self-r") {
            cfg.self_r = parse_double(next_arg(argc, argv, i, a.c_str()), a.c_str());
        } else if (a == "--self-r2") {
            cfg.self_r2 = parse_double(next_arg(argc, argv, i, a.c_str()), a.c_str());
        } else if (a == "--no-self-mask") {
            cfg.self_mask_on = false;
        } else if (a == "--dist-calib") {
            cfg.dist_calib_path = next_arg(argc, argv, i, a.c_str());
        } else if (a == "--dist-hold") {
            cfg.dist_hold = parse_double(next_arg(argc, argv, i, a.c_str()),
                                         a.c_str());
        } else if (a == "--dist-drop") {
            cfg.dist_drop = parse_double(next_arg(argc, argv, i, a.c_str()),
                                         a.c_str());
        } else if (a == "--dist-fill") {
            cfg.dist_fill = parse_double(next_arg(argc, argv, i, a.c_str()),
                                         a.c_str());
        } else if (a == "--gate-width") {
            cfg.gate_width_cm = parse_double(
                next_arg(argc, argv, i, a.c_str()), a.c_str());
            if (cfg.gate_width_cm <= 0)
                fail(a + ": ширина ворот должна быть > 0");
        } else if (a == "--no-dist") {
            cfg.dist_on = false;
        } else if (a == "--rotate") {
            const int v = parse_int(next_arg(argc, argv, i, a.c_str()), a.c_str());
            if (v != 0 && v != 90 && v != 180 && v != 270)
                fail(a + ": ожидалось 0, 90, 180 или 270");
            cfg.rotate = v;
        } else if (a == "--threads") {
            cfg.threads = parse_int(next_arg(argc, argv, i, a.c_str()), a.c_str());
        } else if (a == "--duration") {
            cfg.duration = parse_double(next_arg(argc, argv, i, a.c_str()), a.c_str());
        } else if (a == "--gates-file") {
            cfg.saved_gates_path = next_arg(argc, argv, i, a.c_str());
        } else if (a == "--probe") {
            cfg.probe = true;
        } else if (a == "--snapshot") {
            cfg.snapshot = true;
            cfg.snapshot_file = next_arg(argc, argv, i, a.c_str());
        } else if (a == "--verbose") {
            cfg.verbose = true;
        } else if (a == "--help" || a == "-h") {
            print_usage();
            std::exit(0);
        } else {
            fail("неизвестный ключ " + a);
        }
    }
    return cfg;
}

}  // namespace pi_core
