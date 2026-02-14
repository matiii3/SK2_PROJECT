#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace std;
using Clock = std::chrono::steady_clock;

constexpr size_t MAX_INBUF = 4096;

struct Player {
    int fd = -1;
    uint32_t id = 0;
    std::string nick;
    int score = 0;
    int round_score=0;
    int lives = 0;
    bool active = true; // dajemy false gdy gracz straci wszystkie życia albo będzie offline
    std::string inbuf;  // bufor linii przychodzących
};

struct Config {
    int livesPerPlayer = 6;
    int roundTimeSeconds = 90;
    int minWordLen = 4;
    std::string dictPath = "english_words.txt";
};

struct Game {
    std::string word;               // lowercase [a-z]
    std::string mask;               // '_' i litery
    std::unordered_set<char> used;  // litery już użyte
    Clock::time_point deadline;
    bool active = false;
};

static bool g_running = true;
static void on_sigint(int) { g_running = false; }

// ====== Pomocnicze I/O ======
static bool set_reuseaddr(int fd) {
    int yes = 1;
    return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == 0;
}
static bool set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}
static bool send_all(int fd, const std::string& s) {
    const char* p = s.data();
    size_t left = s.size();
    while (left > 0) {
        ssize_t n = ::send(fd, p, left, 0);
        if (n <= 0) return false;
        p += n; left -= (size_t)n;
    }
    return true;
}
static void send_line(int fd, const std::string& line) {
    std::string msg = line;
    if (msg.empty() || msg.back() != '\n') msg.push_back('\n');
    (void)send_all(fd, msg);
}

// ====== Słownik ======
static vector<string> DICT;
static bool load_dict(const string& path, int minLen) {
    ifstream in(path);
    if (!in) return false;
    string w;
    while (getline(in, w)) {
        string out; out.reserve(w.size());
        for (unsigned char ch : w) {
            if (std::isalpha(ch)) out.push_back((char)std::tolower(ch));
        }
        if ((int)out.size() >= minLen) DICT.push_back(std::move(out));
    }
    return !DICT.empty();
}
static string random_word() {
    static thread_local mt19937 rng{random_device{}()};
    uniform_int_distribution<size_t> dist(0, DICT.size()-1);
    return DICT[dist(rng)];
}

// ====== Serwer – globalny stan (dla prostoty w jednym pliku) ======
struct Server {
    int listenfd = -1;
    fd_set master{};
    int fdmax = -1;

    Config cfg;
    Game game;
    unordered_map<int, Player> by_fd;        // fd -> Player
    unordered_map<uint32_t, int> fd_by_id;   // id -> fd (dla szybkiej wysyłki)
    uint32_t nextId = 1;

    bool init(uint16_t port) {
        listenfd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listenfd < 0) 
        { perror("socket"); return false; }
        if (!set_reuseaddr(listenfd)) 
        { perror("setsockopt"); return false; }
        sockaddr_in addr{}; addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);
        if (bind(listenfd, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return false; }
        if (listen(listenfd, 64) < 0) { perror("listen"); return false; }
        set_nonblock(listenfd);
        FD_ZERO(&master);
        FD_SET(listenfd, &master);
        fdmax = listenfd;
        return true;
    }

    void start_round() {


        size_t ready = std::count_if(by_fd.begin(), by_fd.end(),
            [](const auto& kv) {
                const Player& p = kv.second;
                return p.id != 0;
            }
        );
        
        if(ready<2){
            game.active = false;
            return;
        }

        game.word = random_word();
        game.mask.assign(game.word.size(), '_');
        game.used.clear();
        game.deadline = Clock::now() + chrono::seconds(cfg.roundTimeSeconds);
        game.active = true;

        //resetuje życia i punkty w danej rundzie 
        for (auto& kv : by_fd) {
            auto& pl = kv.second;
            if(pl.id != 0){
                pl.lives = cfg.livesPerPlayer;
                pl.round_score=0;
                pl.active = true;
            }
            else{
                pl.lives=0;
                pl.active=false;

            }
        }
        
        broadcast_start();
    }

    void broadcast_all(const string& line) {
        for (auto& kv : by_fd) {
            if (kv.second.fd != -1) send_line(kv.second.fd, line);
        }
    }

    void broadcast_start() {
        // START mask timeLeft usedLetters players_count [pid nick score lives]...
        auto now = Clock::now();
        uint32_t tl = 0;
        if (game.deadline > now) tl = (uint32_t)chrono::duration_cast<chrono::seconds>(game.deadline - now).count();

        string used_sorted;
        used_sorted.reserve(game.used.size());
        for (char c : game.used) used_sorted.push_back(c);
        sort(used_sorted.begin(), used_sorted.end());
        if (used_sorted.empty()) used_sorted = "-";
        // policz graczy
        size_t n = std::count_if(by_fd.begin(), by_fd.end(),[](const auto& kv) {
            return kv.second.id != 0;
        });

        // składamy jedną linię
        std::string line = "START " + game.mask + " " + to_string(tl) + " " + used_sorted + " " + to_string(n);
        // dorzuć graczy
        for (auto& kv : by_fd) {
            const auto& p = kv.second;
            // uwaga: nick bez spacji (na MVP przyjmijmy takie założenie)
            line += " " + to_string(p.id) + " " + p.nick + " " + to_string(p.score) + " " + to_string(p.lives);
        }
        broadcast_all(line);
    }

    void send_snapshot(int fd) {
        // to samo co START, ale do jednego gracza (np. nowo dołączony)
        auto now = Clock::now();
        uint32_t tl = 0;
        if (game.deadline > now) tl = (uint32_t)chrono::duration_cast<chrono::seconds>(game.deadline - now).count();

        string used_sorted;
        for (char c : game.used) used_sorted.push_back(c);
        sort(used_sorted.begin(), used_sorted.end());
        if (used_sorted.empty()) used_sorted = "-";
        size_t n = std::count_if(by_fd.begin(), by_fd.end(),[](const auto& kv) {
            return kv.second.id != 0;
        });
        std::string line = "START " + game.mask + " " + to_string(tl) + " " + used_sorted + " " + to_string(n);
        for (auto& kv : by_fd) {
            const auto& p = kv.second;
            line += " " + to_string(p.id) + " " + p.nick + " " + to_string(p.score) + " " + to_string(p.lives);
        }
        send_line(fd, line);
    }

    void accept_client() {
        sockaddr_in cli{}; socklen_t cl = sizeof(cli);
        int cfd = ::accept(listenfd, (sockaddr*)&cli, &cl);
        if (cfd < 0) { perror("accept"); return; }
        set_nonblock(cfd);
        FD_SET(cfd, &master);
        fdmax = max(fdmax, cfd);

        // na MVP: dopóki nie zrobi JOIN, trzymamy pusty Player (id=0, brak nicka)
        Player p; p.fd = cfd; p.id = 0; p.active = false;
        by_fd[cfd] = std::move(p);

        // powitanie
        send_line(cfd, "HELLO. Enter: JOIN <nick>  to join the game :)");
    }

    void drop_client(int fd) {
        auto it = by_fd.find(fd);
        if (it != by_fd.end()) {
            string nick = it->second.nick;
            bool wasJoined = it->second.id != 0;
            uint32_t pid = it->second.id;
            string nic= it->second.nick;

            close(fd);
            FD_CLR(fd, &master);
            by_fd.erase(it);

            if (wasJoined) {
                broadcast_all("LEFT " + to_string(pid)+" "+nic);
            }
        } else {
            // fd nieznane
            close(fd);
            FD_CLR(fd, &master);
        }
    }

    bool nick_taken(const string& nick) {
        for (auto& kv : by_fd) {
            if (kv.second.id != 0 && kv.second.nick == nick) return true;
        }
        return false;
    }

    void handle_join(int fd, const string& nickRaw) {
        string nick = nickRaw;
        // walidacja: bez spacji, ascii, 1..20 znaków
        if (nick.empty() || nick.size() > 20) { send_line(fd, "JOIN_REJECT bad_nick (1-20 letters only)"); return; }
        for (char ch : nick) {
            if (ch == ' ') { send_line(fd, "JOIN_REJECT bad_nick (space)"); return; }
        }
        if (nick_taken(nick)) { send_line(fd, "JOIN_REJECT nick_taken"); return; }

        auto it = by_fd.find(fd);
        if (it == by_fd.end()) return; // fd już padło?

        Player& p = it->second;
        if (p.id != 0) { send_line(fd, "JOIN_REJECT already_joined"); return; }

        p.id = nextId++;
        p.nick = nick;
        p.score = 0;                  // nowy gracz startuje z 0 punktów
        p.lives = cfg.livesPerPlayer; // pełne życie w aktualnej rundzie
        p.active = true;

        fd_by_id[p.id] = fd;

        send_line(fd, "JOIN_ACCEPT " + to_string(p.id));
        // nowy gracz dostaje snapshot i może od razu grać
        send_snapshot(fd);

        // wszyscy widzą info o wejściu
        broadcast_all("JOINED " + to_string(p.id)+"." + p.nick);

        if(!game.active){
            size_t ready = count_if(by_fd.begin(), by_fd.end(),[](auto& kv){return kv.second.id != 0;});
            
        
        if(ready>1)start_round();
        }
    }

    void handle_guess(int fd, const string& arg) {
        if (arg.size() != 1) { send_line(fd, "REJECT ? invalid"); return; }
        char letter = (char)std::tolower((unsigned char)arg[0]);
        if (letter < 'a' || letter > 'z') { send_line(fd, "REJECT ? invalid"); return; }

        auto it = by_fd.find(fd);
        if (it == by_fd.end() || it->second.id == 0) { send_line(fd, "REJECT ? not_joined"); return; }
        uint32_t pid = it->second.id;

        if (!game.active) {
            send_line(fd, string("REJECT ")+letter+" round_closed");
            return;
        }
        if (game.used.count(letter)) {
            send_line(fd, string("REJECT ")+letter+" already_used");
            return;
        }

        if(!(it->second.active) && it->second.lives<=0){
            send_line(fd,string("REJECT ")+letter+ " dead_player");
            return;
        }

        // zarejestruj użycie (chroni przed duplikacją przy wyścigu)
        game.used.insert(letter);

        // sprawdź w słowie
        vector<int> pos;
        for (int i = 0; i < (int)game.word.size(); ++i) {
            if (game.word[i] == letter) {
                pos.push_back(i);
                game.mask[i] = letter;
            }
        }

        if (!pos.empty()) {
            // punkt +1 dla zgłaszającego (niezależnie od liczby wystąpień)
            it->second.score += 1;
            it->second.round_score+=1;
            // Broadcast: ACCEPT <letter> <pid> <newMask> <positions_count> p1 p2 ...
            string line = string("ACCEPT ") + letter + " " + to_string(pid) + " " + game.mask + " " + to_string(pos.size());
            for (int p : pos) line += " " + to_string(p);
            broadcast_all(line);

            // po każdej akcji dobrze też wysłać szybki stan graczy
            broadcast_players_state();

        } else {
            // Zły strzał → -1 życie TYLKO dla zgłaszającego
            it->second.lives = max(0, it->second.lives - 1);
            if (it->second.lives == 0) it->second.active = false;

            send_line(fd, string("REJECT ") + letter + " not_in_word");
            broadcast_all(string("MISS ") + letter + " " + to_string(pid));
            if (it->second.lives == 0) {
                broadcast_all("ELIM " + to_string(pid));
            }
            // odśwież lives/scores u wszystkich
            broadcast_players_state();
        }
    }

    void broadcast_players_state() {

        std::vector<Player*> score_sorted;
        for( auto& kv : by_fd)
        {
            if(kv.second.id!=0) score_sorted.push_back(&kv.second);
        }
        std::sort(score_sorted.begin(),score_sorted.end(),[](Player*a, Player*b){
            return a->score > b->score;
        });
        
        string line = "PLAYERS " + to_string(score_sorted.size());
        for (Player* p:score_sorted) 
        {
            line += " " + to_string(p->id) + " " + p->nick+" "+ to_string(p->score) + " " + to_string(p->lives);
        }
        broadcast_all(line);
    }

    vector<Player*> round_winner(){
        int max_round_score = -1;
        vector<Player*> winners;
            for (auto& kv : by_fd) 
            {
                Player& p = kv.second;
                if (p.id == 0) continue;
                
                if (p.round_score > max_round_score) {
                    max_round_score = p.round_score;
                    winners.clear();
                    winners.push_back(&p);
                } else if (p.round_score == max_round_score) {
                    winners.push_back(&p);
                }
            }
        return winners;

    }

    void maybe_end_round() {
        if (!game.active) return;

        if (game.mask == game.word) {
            
            broadcast_all(string("GAME OVER word reavealed: ") + game.word);
            
            auto winner=round_winner();
            if(winner.size()==1){
                auto top1 = winner.front();
                broadcast_all(string("Round won by: ") + to_string(top1->id)+"."+top1->nick+" with score: "+ to_string(top1->round_score));
            }
            start_round(); // nowa runda
            return;
        }
        // warunek: ostatni aktywny
        int joinCount = 0;
        int aliveCount = 0;
        for (auto& kv : by_fd) {
            const auto& p = kv.second;
            if (p.id != 0){
                 joinCount++;
                 if(p.lives>0)aliveCount++;

            }
        }
        if (joinCount <= 1) {
            broadcast_all(string("GAME OVER - NOT ENOUGH PLAYERS ONLINE, WORD WAS: ") + game.word);
            start_round();
            return;
        }
        if (aliveCount < 1) {
            broadcast_all(string("GAME OVER - ALL PLAYERS DIED, WORD WAS:") + game.word);
            start_round();
            return;
        }
        if (Clock::now() >= game.deadline) {
            broadcast_all(string("GAME OVER - timeout \n") +string("the word that nobody guessed:") + game.word);
            start_round();
            return;
        }
    }

    void handle_line(int fd, const string& lineRaw) {
        // Prosty parsing: KOMENDA [arg...]
        // JOIN <nick>
        // GUESS <letter>
        // QUIT
        string line = lineRaw;
        // usuń \r\n
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty()) return;
        // split pierwsze słowo
        size_t sp = line.find(' ');
        string cmd = (sp == string::npos ? line : line.substr(0, sp));
        string arg = (sp == string::npos ? "" : line.substr(sp+1));

        for (auto& c : cmd) c = (char)std::tolower((unsigned char)c);

        if (cmd == "join") {
            handle_join(fd, arg);
        } else if (cmd == "guess") {
            handle_guess(fd, arg);
        } else if (cmd == "quit") {
            send_line(fd,"quit_ok");
            drop_client(fd);
        } else {
            send_line(fd, "ERR unknown_command");
        }
    }

    void recv_and_process(int fd) {
        char buf[4096];
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) {
            if (n == 0 || (n < 0 && errno != EWOULDBLOCK && errno != EAGAIN)) {
                // rozłączenie
                drop_client(fd);
            }
            return;
        }
        auto& pl = by_fd[fd];
        pl.inbuf.append(buf, (size_t)n);
        
        if(pl.inbuf.size()>MAX_INBUF){
            drop_client(fd);
            return;
        }

        for (;;) {
            size_t pos = pl.inbuf.find('\n');
            if (pos == string::npos) break;
            string one = pl.inbuf.substr(0, pos+1);
            pl.inbuf.erase(0, pos+1);
            handle_line(fd, one);
        }
    }

    void loop() {
        // start pierwszej rundy po odpaleniu
        start_round();

        while (g_running) {
            fd_set readfds = master;
            // timeout do sprawdzania końca rundy
            timeval tv{1,0};
            int r = select(fdmax+1, &readfds, nullptr, nullptr, &tv);
            if (r < 0) {
                if (errno == EINTR) continue;
                perror("select");
                break;
            }
            for (int fd = 0; fd <= fdmax; ++fd) {
                if (!FD_ISSET(fd, &readfds)) continue;
                if (fd == listenfd) {
                    accept_client();
                } else {
                    recv_and_process(fd);
                }
            }
            // po każdym cyklu sprawdź warunki końca rundy
            maybe_end_round();
        }

        // sprzątaj
        for (int fd = 0; fd <= fdmax; ++fd) {
            if (FD_ISSET(fd, &master)) close(fd);
        }
    }
};

int main(int argc, char** argv) {
    signal(SIGINT, on_sigint);

    uint16_t port = 55555;
    if (argc > 1) port = (uint16_t)stoi(argv[1]);

    Config cfg;
    // Możesz wczytać config z pliku — dla MVP używamy domyślnych wartości
    if (!load_dict(cfg.dictPath, cfg.minWordLen)) {
        cerr << "Failed to load dictionary: " << cfg.dictPath << "\n";
        cerr << "Put an english_words.txt (one lowercase word per line)\n";
        return 1;
    }

    Server s;
    s.cfg = cfg;
    if (!s.init(port)) {
        cerr << "Failed to init server on port " << port << "\n";
        return 1;
    }

    cout << "Hangman server listening on port " << port << "\n";
    s.loop();
    return 0;
}
