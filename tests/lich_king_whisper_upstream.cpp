// Pinned be923cbc45c3: excerpt of zone_the_scarlet_enclave.cpp (AzerothCore, AGPL-3.0).
enum spells_lich_king_whisper
{
    SPELL_LICH_KING_VO_BLOCKER = 58207,
    SPELL_LICHKINGDK001 = 58208,
    SPELL_LICHKINGDK002 = 58209,
    SPELL_LICHKINGDK003 = 58210,
    SPELL_LICHKINGDK004 = 58211,
    SPELL_LICHKINGDK005 = 58212,
    SPELL_LICHKINGDK006 = 58213,
    SPELL_LICHKINGDK007 = 58214,
    SPELL_LICHKINGDK008 = 58215,
    SPELL_LICHKINGDK009 = 58216,
    SPELL_LICHKINGDK010 = 58217,
    SPELL_LICHKINGDK011 = 58218,
    SPELL_LICHKINGDK012 = 58219,
    SPELL_LICHKINGDK013 = 58220,
    SPELL_LICHKINGDK014 = 58221,
    SPELL_LICHKINGDK015 = 58222,
    SPELL_LICHKINGDK016 = 58223
};

//spell 58207 rand Whisper
class spell_lich_king_vo_blocker : public AuraScript
{
    PrepareAuraScript(spell_lich_king_vo_blocker);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo
        ({
             SPELL_LICHKINGDK001, SPELL_LICHKINGDK002, SPELL_LICHKINGDK003, SPELL_LICHKINGDK004,
             SPELL_LICHKINGDK005, SPELL_LICHKINGDK006, SPELL_LICHKINGDK007, SPELL_LICHKINGDK008,
             SPELL_LICHKINGDK009, SPELL_LICHKINGDK010, SPELL_LICHKINGDK011, SPELL_LICHKINGDK012,
             SPELL_LICHKINGDK013, SPELL_LICHKINGDK014, SPELL_LICHKINGDK015, SPELL_LICHKINGDK016
        });
    }

    void HandleEffectApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (Player* target = GetTarget()->ToPlayer())
        {
            //spell 58208-58223
            GetCaster()->CastSpell(target, urand(SPELL_LICHKINGDK001, SPELL_LICHKINGDK016), true);
        }
    }

    void Register() override
    {
        OnEffectApply += AuraEffectApplyFn(spell_lich_king_vo_blocker::HandleEffectApply, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

