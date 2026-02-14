#include <wx/wx.h>
#include <wx/listctrl.h>
#include <thread>
#include <atomic>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sstream>
#include <fcntl.h>
#include <errno.h>


struct GameState {
    wxString mask = "_ _ _ _";
    wxString status = "Polaczono. Podaj nick.";
    wxString logs = "";
};

class MyFrame : public wxFrame {
public:
    MyFrame() : wxFrame(NULL, wxID_ANY, "Wisielec Network Game", wxDefaultPosition, wxSize(800, 600)) {
        SetupUI();
        //ConnectToServer();
    }

    ~MyFrame() {
        running = false;
        if (netThread.joinable()) netThread.join();
        if(connectThread.joinable())connectThread.join();
        if (sock >= 0) close(sock);
    }

private:    
    wxTextCtrl* txtInput;
    wxButton* btnSend;
    wxStaticText* lblMask;    
    wxStaticText* lblStatus;   
    wxStaticText* lifeinfo;
    wxStaticText* lblUsedLetters;
    wxTextCtrl* txtLog;         
    wxListCtrl* listPlayers;  
    wxGauge* pasek;
    wxButton* btnExit;
    wxTextCtrl* txtIP;
    wxTextCtrl* txtPort;
    wxButton* btnConnect;



    int sock = -1;
    std::thread netThread;
    std::thread connectThread;
    std::atomic<bool> running{true};
    std::string incompleteLine; // bufor na urwane pakiety
    bool joined = false;
    std::string my_name = "";
    std::string used_letters;
     

    void SetupUI() {
        wxPanel* panel = new wxPanel(this, wxID_ANY);
        wxBoxSizer* vbox = new wxBoxSizer(wxVERTICAL);

        wxBoxSizer* connSizer = new wxBoxSizer(wxHORIZONTAL);

        txtIP = new wxTextCtrl(panel, wxID_ANY,"127.0.0.1");
        txtPort = new wxTextCtrl(panel, wxID_ANY,"55555");

        btnConnect = new wxButton(panel, wxID_ANY, "Polacz");

        connSizer->Add(new wxStaticText(panel, wxID_ANY,"IP:"),0,wxALIGN_CENTER_VERTICAL | wxRIGHT,5);
        connSizer->Add(txtIP,1,wxRIGHT,10);

        connSizer->Add(new wxStaticText(panel, wxID_ANY,"PORT:"),0,wxALIGN_CENTER_VERTICAL | wxRIGHT,5);
        connSizer->Add(txtPort,1,wxRIGHT,10);

        connSizer -> Add(btnConnect,0);

        vbox->Add(connSizer, 0, wxEXPAND | wxALL, 5);


        lblMask = new wxStaticText(panel, wxID_ANY, "CONNECTING...", wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER);
        wxFont fontMask = lblMask->GetFont();
        fontMask.MakeBold().Scale(3.0);
        lblMask->SetFont(fontMask);
        vbox->Add(lblMask, 0, wxEXPAND | wxALL, 20);
        
        lifeinfo = new wxStaticText(panel,wxID_ANY,"Pozostalo ci tyle zycia:",wxPoint(360,42));
        lifeinfo->Hide();
        pasek = new wxGauge(panel,wxID_ANY,6,wxPoint(360,50),wxSize(200, 20),wxGA_HORIZONTAL | wxGA_SMOOTH);
        pasek->SetMinSize(wxSize(200, 20));
        pasek->Hide();
        
        wxBoxSizer* topBar = new wxBoxSizer(wxHORIZONTAL);
        topBar->AddStretchSpacer(1);
        topBar->Add(lifeinfo, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
        topBar->Add(pasek, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
        vbox->Add(topBar, 0, wxEXPAND);


        lblStatus = new wxStaticText(panel, wxID_ANY, "Czekam na serwer...");
        vbox->Add(lblStatus, 0, wxALIGN_CENTER | wxBOTTOM, 10);
        
        lblUsedLetters = new wxStaticText(panel, wxID_ANY, "Uzyte litery: -");
        vbox->Add(lblUsedLetters, 0, wxALIGN_CENTER | wxBOTTOM, 10);
        
        
        wxBoxSizer* hbox = new wxBoxSizer(wxHORIZONTAL);      
        listPlayers = new wxListCtrl(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT);
        listPlayers->InsertColumn(0, "ID", wxLIST_FORMAT_LEFT, 50);
        listPlayers->InsertColumn(1, "Nick", wxLIST_FORMAT_LEFT, 100);
        listPlayers->InsertColumn(2, "Score", wxLIST_FORMAT_LEFT, 60);
        listPlayers->InsertColumn(3, "Lives", wxLIST_FORMAT_LEFT, 60);
        hbox->Add(listPlayers, 1, wxEXPAND | wxRIGHT, 5);

        txtLog = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE);
        hbox->Add(txtLog, 1, wxEXPAND | wxLEFT, 5);
        
        vbox->Add(hbox, 1, wxEXPAND | wxALL, 10);

        
        wxBoxSizer* inputSizer = new wxBoxSizer(wxHORIZONTAL);
        txtInput = new wxTextCtrl(panel, wxID_ANY, "Player1", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
        btnSend = new wxButton(panel, wxID_ANY, "zatwierdz");
        btnExit = new wxButton(panel, wxID_ANY, "Wyjdz z gry");
        inputSizer->Add(txtInput, 1, wxEXPAND | wxRIGHT, 5);
        inputSizer->Add(btnSend, 0);
        inputSizer->Add(btnExit,0);
        vbox->Add(inputSizer, 0, wxEXPAND | wxALL, 10);

        panel->SetSizer(vbox);
        
        btnConnect-> Bind(wxEVT_BUTTON, &MyFrame::OnConnect,this);
        btnSend->Bind(wxEVT_BUTTON, &MyFrame::OnSend, this);
        txtInput->Bind(wxEVT_TEXT_ENTER, &MyFrame::OnSend, this);
        btnExit->Bind(wxEVT_BUTTON, &MyFrame::OnExitGame, this);
        Bind(wxEVT_CLOSE_WINDOW, &MyFrame::OnWindowClose, this);
    }

    void OnConnect(wxCommandEvent&){

        if(sock>=0){
            txtLog->AppendText("Juz polaczono.\n");
            return;
        }
        wxString ip = txtIP->GetValue();
        wxString portStr = txtPort->GetValue();

        long portLong;
        if(!portStr.ToLong(&portLong)){
            txtLog->AppendText("Nieprawidlowy port\n");
            return;
        }

        ConnectToServer(ip.ToStdString(),(uint16_t)portLong);

    }

    void ConnectToServer(const std::string& ip, uint16_t port) {

        lblStatus->SetLabel("Laczenie...");
        connectThread = std::thread(&MyFrame::ConnectWorker, this,ip,port);
        connectThread.detach();

    }


    void ConnectWorker(std::string ip, uint16_t port){

        int fd = socket(AF_INET, SOCK_STREAM,0);
        if(fd < 0 ){
            CallAfter([this](){
                lblStatus->SetLabel("Nie mozna utworzyc socketu");
            });
            return;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if(inet_pton(AF_INET,ip.c_str(),&addr.sin_addr)<=0){
            CallAfter([this](){
                lblStatus->SetLabel("Nieprawidlowy adres IP");
            });
            close(sock);
            sock = -1;
            return;
        }

        if(connect(fd,(sockaddr*)&addr, sizeof(addr))<0){
            close(fd);
            CallAfter([this](){
                lblStatus->SetLabel("Nie mozna polaczyc sie z serwerem");
            });
            return;

        }

        sock=fd;
        running= true;

        CallAfter([this](){
                txtLog->AppendText("Polaczono z serwerem.\n");
                lblMask->SetLabel("Serwer: Wisielec - Gra");
            });
        netThread = std::thread(&MyFrame::NetworkLoop, this);

    }


    void OnSend(wxCommandEvent& event) {
        wxString val = txtInput->GetValue();
        if (val.IsEmpty()) return;

        std::string cmd;
        std::string input = val.ToStdString();
        for (auto &ch : input) {
            ch = std::tolower(static_cast<unsigned char>(ch));
        }       
        if (joined){
            
            if (input=="quit") {
                cmd = "quit\n";
            }
            else{
                cmd="GUESS "+input+"\n";
            }
        }
        else{
                cmd = "JOIN " + input + "\n";
                my_name = input;
            }

        send(sock, cmd.c_str(), cmd.length(), 0);
        txtInput->Clear();
        txtInput->SetFocus();
    }

  
    

    
    
    bool StartsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), s.begin());
    }
    void UpdateUsedLettersDisplay()
    {
        wxString txt = "Uzyte litery: ";
        if (used_letters.empty())
            txt += "-";
        else
        {
            for (char c : used_letters)
            {
                txt << c << " ";
            }
        }

        lblUsedLetters->SetLabel(txt);
    }

    void HandleServerMessage(std::string line) {

        std::stringstream ss(line);
        std::string cmd;
        ss >> cmd;

        if (cmd == "REJECT") {
                std::string arg, reason;
                ss >> arg >> reason;
                wxString msg;
                
                if(reason == "not_in_word"){
                    txtLog->AppendText("Strzal nietrafiony - '"+arg+ "' nie ma w hasle\n");
                    return;
                }
                if(reason == "round_closed"){
                    txtLog->AppendText("Poczekaj na start rundy\n");
                    return;
                }
                if(reason == "invalid")
                {
                   txtLog->AppendText("Nieprawidlowy znak\n");
                   return;
                }
                if(reason== "not_joined")
                {
                   txtLog->AppendText("Najpierw dolacz do gry\n");
                   return;
                }
                if(reason== "already_used")
                {
                   txtLog->AppendText("W tej rundzie juz uzyto ten znak\n");
                   return;
                }
                if(reason == "dead_player")
                {
                    txtLog->AppendText("Straciles wszytskie zycia\n");
                    return;
                }
                txtLog->AppendText(line + "\n");
                
            }
        if (StartsWith(line, "HELLO.")) {
            
            txtLog->AppendText("Witaj\nPodaj nick aby dolaczyc do gry\n");
            if (txtInput->GetValue().IsEmpty()) {
                txtInput->SetValue("TwojNick");
                txtInput->SetSelection(-1, -1);
                }
            return;
        }else if(cmd == "MISS"){
            std::string letter,pid;
            ss >> letter >> pid;
            if (used_letters.find(letter[0]) == std::string::npos)
                used_letters.push_back(letter[0]);

            UpdateUsedLettersDisplay(); 
            
            txtLog->AppendText("Gracz:"+pid+" spudlowal w: "+letter+ "\n");
            return;

        }else if (cmd == "quit_ok") {
            joined = false;
            pasek->Hide();
            lblStatus->SetLabel("Rozlaczono z serwerem.\n Prosze zrestartuj gre, aby polaczyc sie ponownie");
            txtLog->AppendText("Rozlaczono.\n");
            return;
        }else if(cmd == "JOIN_ACCEPT"){
            
            joined=true;
            txtLog->AppendText("Dolaczono pomyslnie.\nNiebawem rozpocznie sie runda\n");
            return;
        }else if(cmd =="JOINED"){

            std::string pid, pnick;
            ss >> pid >> pnick;
            txtLog->AppendText("Polaczony: "+pid+" "+pnick+"\n");
            return;
        }else if (cmd == "ACCEPT") {
            std::string c, pid, newMask;
            ss >> c >> pid >> newMask;
            
            if (used_letters.find(c[0]) == std::string::npos)
                used_letters.push_back(c[0]);

            UpdateUsedLettersDisplay(); 

            wxString prettyMask = "";
            for(char m : newMask) { prettyMask << m << " "; }
            lblMask->SetLabel(prettyMask);
            
            txtLog->AppendText("Gracz " + pid + " odgadl " + c + "!\n");
            return;
        }else if(cmd == "START") {
                
                std::string mask, time, used, p_count;
                ss >> mask >> time >> used >> p_count;

                if (used == "-")
                    used_letters.clear();
                else
                    used_letters = used;

                UpdateUsedLettersDisplay();

                pasek->Show();
                lifeinfo->Show();
                wxString fixdMask = "";
                for(char c : mask) { fixdMask << c << " "; }
                lblMask->SetLabel(fixdMask);
                
                lblStatus->SetLabel("Czas: " + time + "s | Gracze online: " + p_count);
                ParsePlayers(ss);
                return;

        }else if (cmd == "PLAYERS") {

            std::string n_str;
            ss >> n_str;
            ParsePlayers(ss);

        }else{
        txtLog->AppendText(line + "\n");
        }
        
    }

    void ParsePlayers(std::stringstream& ss) {
        listPlayers->DeleteAllItems();
        int idStr, lives;
        std::string nick, score;
        
        int row = 0;
        while (ss >> idStr >> nick >> score>> lives) {
            
             long idx = listPlayers->InsertItem(row, std::to_string(idStr));
             
             listPlayers->SetItem(idx, 1, nick);
             listPlayers->SetItem(idx, 2, score);
             listPlayers->SetItem(idx, 3, std::to_string(lives));
             if(nick==my_name){                
                pasek -> SetValue(lives);
             }
             row++;
        }
    }

    void DisconnectFromServer(){
        if (sock >= 0)
        {
            if (joined)
            {
                std::string cmd = "quit\n";
                send(sock, cmd.c_str(), cmd.length(), 0);
            }

            shutdown(sock, SHUT_RDWR);
            close(sock);
            sock = -1;
        }
        running = false;

        if (netThread.joinable())
            netThread.join();
    }
    void OnExitGame(wxCommandEvent& event)
    {
        DisconnectFromServer();
        Destroy();   // zamyka okno
    }
    void OnWindowClose(wxCloseEvent& event)
    {
        DisconnectFromServer();
        Destroy();
    }
};

class MyApp : public wxApp {
public:
    virtual bool OnInit() {
        MyFrame* frame = new MyFrame();
        frame->Show(true);
        return true;
    }
};

wxIMPLEMENT_APP(MyApp);
