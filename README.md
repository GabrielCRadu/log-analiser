# Log Analiser

Proiect individual la disciplina Metode avansate de programare, anul universitar 2026-2027.

## Autor

- **Nume:** Radu Gabriel-Claudiu
- **Grupa:** 2.1
- **Marca:** LH715705
- **Tema:** Tema 9 - Analizor de log-uri

## Descriere

Aplicatia este un serviciu REST care primeste linii de log, le parseaza si permite interogarea lor: filtrare dupa nivel (INFO, WARN, ERROR etc.) si interval de timp, statistici globale si detectarea rafalelor de erori printr-o fereastra glisanta fara suprapunere. Rezolva problema analizei rapide a unor fisiere de log mari, fara a le parcurge manual. [?]

## Tehnologii

C++20 cu cpp-httplib si nlohmann/json

(restul sectiunilor ramane identic pana la tabel)

## Rutele implementate

| Ruta | Metoda | Descriere |
|---|---|---|
| `/health` | GET | Starea serviciului |
| `/version` | GET | Versiunea si commit-ul din care a fost construita imaginea |
| `/` | GET | Pagina de prezentare |
| `/reset` | POST | Goleste datele din memorie |
| `/logs` [?] | POST | Primeste si parseaza liniile de log |
| `/logs` [?] | GET | Intoarce liniile filtrate dupa nivel si interval de timp |
| `/stats` [?] | GET | Statistici globale |
| `/bursts` [?] | GET | Rafalele de erori detectate |

## Decizii de implementare

- **C++20 cu cpp-httplib si nlohmann/json:** biblioteci header-only, usor de integrat in CMake si in imaginea Docker, fara un framework greu. [?]
- **Fereastra glisanta fara suprapunere pentru rafale:** fiecare eroare este numarata o singura data, deci o rafala nu este raportata de mai multe ori. Complexitate liniara. [?]
- **Date tinute in memorie:** simplifica implementarea, iar `/reset` permite repornirea analizei fara a reporni containerul. [?]
