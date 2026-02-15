# Wisielec 
## Opis


Gracz łączy się do serwera wpisując ip oraz port. 

Gracz podaje swój nick (jeśli nick jest zajęty, serwer prosi o podanie innego).

Po zalogowaniu dołącza do jednej wspólnej gry, w której rozgrywane są kolejne rundy „Wisielca”.

W każdej rundzie serwer losuje angielskie hasło (słowo składające się wyłącznie z liter A–Z) i rozsyła do wszystkich graczy jego zamaskowaną postać (np. _ _ _ dla „car”). 

Wszyscy gracze wspólnie odgadują litery tego samego hasła. Obowiązuje zasada „kto pierwszy – ten lepszy”: 

jeśli gracz jako pierwszy zgłosi literę, która występuje w haśle, otrzymuje 1 punkt (1 punkt za 1 literę niezależnie od liczby jej wystąpień w haśle). 

Jeżeli zgłoszona litera nie występuje w haśle, zgłaszający traci 1 życie. Każdy gracz ma własną pulę żyć (własnego „wisielca”); wszyscy widzą stan żyć wszystkich graczy.

Gracz może dołączyć w trakcie trwającej rundy -> po dołączeniu otrzymuje bieżącą maskę hasła, listę już zgłoszonych liter, aktualny ranking oraz stan żyć graczy i pozostały czas rundy; od razu może zgłaszać litery. 

Nowy gracz startuje z 0 punktów. 

Gracz może również w dowolnym momencie opuścić grę; rozgrywka toczy się dalej, o ile po odejściu gracza nadal pozostaje minimalna liczba aktywnych graczy
Runda kończy się, gdy:

-całe hasło zostało odsłonięte, lub

-wszystkim graczom skończyły się życia, lub

-upłynął skonfigurowany limit czasu rundy, lub

-ktoś opuścił serwer i  pozostałeś jedynym aktywnym graczem na serwerze 


Po zakończeniu rundy serwer rozsyła podsumowanie , po czym zaczyna sie kolejna runda z nowym hasłem, w której każdy gracz z powrotem ma maksymalną liczbę żyć.

Liczba punktów ukazana w rankingu jest punktacją wszech czasów. Natomiast serwer gromadzi również informacje na temat zdobytychpunktów w danej rundzie; jeśli jedna osoba uzbierała tych punktów najwięcej to wygrywa rundę (jeśli dwie osoby odgadły najwięcej punktów to nie wygrywa nikt - nie ma remisów)

Gra trwa w sposób ciągły, dopóki na serwerze są gracze.
