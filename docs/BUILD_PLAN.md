# План сборки WoW 3.3.5a

Дата выбора: 2026-11-11.
Цель: локальный сервер Windows x64, сборка через GitHub Actions.

## Основа

- AzerothCore: https://github.com/mod-playerbots/azerothcore-wotlk, ветка Playerbot.
- Playerbots: https://github.com/mod-playerbots/mod-playerbots.
- AHBot: https://github.com/azerothcore/mod-ah-bot.

## Выбраны пользователем

- Reforging: https://github.com/silviu20092/mod-reforging.
- NPC Teleporter: конкретная реализация ещё выбирается; должна учитывать ограничения Individual Progression.
- Transmog: https://github.com/azerothcore/mod-transmog.
- Individual Progression: https://github.com/ZhengPeiRu21/mod-individual-progression.

## Предложены, окончательное включение не зафиксировано

- Dungeon Clear: https://github.com/jrad7/mod-dungeon-clear.
- Клиентские аддоны MultiBot и Dungeon Clear.

## Дополнительные расы: исследование, не установка

- Worgoblin: https://github.com/Helias/mod-worgoblin (воргены и гоблины).
- High Elf: https://github.com/abracadaniel22/azerothcore-highelf (заявлена поддержка Playerbots; имеется патч Individual Progression).
- WHE: https://github.com/Medviten/WHE-32-bit (экспериментальная интеграция нескольких рас, правок клиента, DBC и ядра).
- Пользователь пока попросил проверить наличие таких проектов, а не устанавливать расы.

## Проверки до готового выпуска

Совместимость выбранных ревизий пока не проверена. Компиляция не запущена.
Нужны фиксация ревизий, Windows workflow, компиляция, проверка запуска/SQL и упаковка.
Особое внимание: перековка и изменения предметов Individual Progression; телепортер и этапы доступа; для новых рас — совпадение клиентских и серверных DBC, создание персонажа, стартовые данные и Playerbots.
