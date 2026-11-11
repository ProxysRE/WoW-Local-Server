# WoW Local Server

AzerothCore 3.3.5a под Windows x64 для локальной игры с Playerbots.

**Состав:** Playerbots, AHBot, Reforging, NPC Teleporter, Transmog, Individual Progression.
Новых рас нет. В экспериментальной ветке добавлен пилот арены 1×1 против Playerbot.
Подробности: [пилот арены](docs/ARENA_1V1_PILOT_RU.md). Игровые проверки ещё не выполнены.

## Получение сборки

Откройте **Actions → Windows server build**. После успешного завершения скачайте
артефакт WoW-Local-Server-Windows-x64-N. До первого успешного прогона готовых бинарников нет.

Версии закреплены в sources.lock.json. Сборка запускается при изменении
сборочных файлов в main или кнопкой **Run workflow**.

Артефакт содержит EXE, инструменты извлечения данных, конфиги, SQL,
точные исходники и лицензии. MySQL-сервис устанавливается отдельно.
Данные maps, vmaps, mmaps и dbc нужно извлечь из собственного клиента.

Читайте [инструкцию запуска](docs/SETUP_RU.md) и [состав сборки](docs/BUILD_PLAN.md).

## Проверки

Workflow проверяет компиляцию и загрузку обоих EXE с --version.
Игровая проверка и применение всех SQL остаются отдельным этапом.

## Авторы

Ядро — AzerothCore / команда mod-playerbots; Playerbots — команда mod-playerbots;
AHBot и Transmog — авторы проектов AzerothCore; Reforging — silviu20092;
Individual Progression — ZhengPeiRu21 и соавторы;
телепортер — Zoidwaffle, на основе работы Rochet2.
Скрипт установки зависимостей адаптирован из Windows workflow закреплённого форка ядра.
Лицензии исходных проектов сохраняются в архиве вместе с исходным кодом.
