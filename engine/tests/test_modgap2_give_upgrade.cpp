// GiveUpgradeUpdate, both editions: delivery RW 0x8A01B2; BFME2's update
// is Rva 0x49C93F (GiveUpgradeUpdate.cpp). The carrier supplies its own upgrade.
#include "doctest.h"
#include "CombatTestUtil.h"
#include "Common/SpecialPower.h"
#include "Common/Upgrade.h"
#include "GameLogic/Module/GiveUpgradeUpdate.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/PartitionManager.h"

namespace {
const char kObjects[] =
    "SpecialPower TestGiveUpgradePower\n Enum = SPECIAL_GIVE_UPGRADE\n ReloadTime = 30000\nEnd\n"
    "Object TestUpgradeCarrier\n KindOf = INFANTRY SELECTABLE\n"
    " Body = ActiveBody BodyTag\n MaxHealth = 100\n End\n"
    " Behavior = AIUpdateInterface AITag\n End\n"
    " Behavior = SpecialPowerModule PowerTag\n SpecialPowerTemplate = TestGiveUpgradePower\n UpdateModuleStartsAttack = Yes\n End\n"
    " Behavior = GiveUpgradeUpdate GiveTag\n SpecialPowerTemplate = TestGiveUpgradePower\n StartAbilityRange = 100\n"
    " IgnoreFacingCheck = Yes\n ApproachRequiresLOS = No\n UnpackTime = 200\n PreparationTime = 200\n PackTime = 200\n DeliverUpgrade = No\n End\nEnd\n"
    "Object TestUpgradeRecipient\n KindOf = INFANTRY SELECTABLE\n"
    " Body = ActiveBody BodyTag\n MaxHealth = 100\n End\n"
    " Behavior = AIUpdateInterface AITag\n End\n"
    " Behavior = StatusBitsUpgrade UpgradeTag\n TriggeredBy = Upgrade_Barracks2\n End\nEnd\n"
    "Object TestUpgradeHorde\n KindOf = HORDE SELECTABLE\n"
    " Body = ActiveBody BodyTag\n MaxHealth = 100\n End\n"
    " Behavior = HordeContain ContainTag\n Slots = 2\n End\n"
    " Behavior = StatusBitsUpgrade UpgradeTag\n TriggeredBy = Upgrade_Barracks2\n End\nEnd\n";

struct GiveWorld : combattest::CombatWorld {
    SpecialPowerStore powers;
    SpecialPowerStore *previous = TheSpecialPowerStore;
    explicit GiveWorld(bool automatic = false) {
        TheSpecialPowerStore = &powers;
        w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
        std::string objects = kObjects;
        if (automatic) {
            const size_t p = objects.find("DeliverUpgrade = No");
            REQUIRE(p != std::string::npos);
            objects.replace(p, std::string("DeliverUpgrade = No").size(), "DeliverUpgrade = Yes");
        }
        const std::string error = w.load(objects);
        REQUIRE_MESSAGE(error.empty(), error);
    }
    ~GiveWorld() { TheSpecialPowerStore = previous; }
};
}

TEST_CASE("modgap2 give upgrade: the carrier delivers its upgrade to a valid target")
{
    for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 }) {
        GiveWorld w;
        w.logic->setEdition(edition);
        CAPTURE(gameEditionName(edition));
        Object *carrier = w.unit("TestUpgradeCarrier", 'A', 300, 300);
        Object *recipient = w.unit("TestUpgradeRecipient", 'A', 305, 300);
        REQUIRE(carrier);
        REQUIRE(recipient);
        const UpgradeTemplate *upgrade = TheUpgradeCenter->findUpgrade("Upgrade_Barracks2");
        REQUIRE(upgrade);
        carrier->giveUpgrade(upgrade);
        REQUIRE(carrier->hasUpgrade(upgrade));
        REQUIRE(recipient->affectedByUpgrade(upgrade));
        auto *ability = dynamic_cast<SpecialAbilityUpdate *>(carrier->findModule("GiveUpgradeUpdate"));
        REQUIRE(ability);
        ability->initiateIntentToDoSpecialPower(ability->abilityData()->m_specialPowerTemplate, recipient, nullptr, 0, 0);
        w.frames(20);
        CHECK(recipient->hasUpgrade(upgrade));
        bool stopped = false;
        for (const auto &line : w.logic->report().stops) stopped = stopped || line == GiveUpgradeUpdate::stopLine();
        CHECK_FALSE(stopped); // PORT-199: the ordinary target path is ported, no S-6011 line
        const ObjectID carrierId = carrier->getID();
        w.frames(50);
        CHECK(w.logic->findObjectByID(carrierId) == nullptr);
    }
}

TEST_CASE("modgap2 give upgrade: automatic delivery chooses the nearest eligible ally")
{
    for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 }) {
        GiveWorld w(true);
        w.logic->setEdition(edition);
        CAPTURE(gameEditionName(edition));
        Object *carrier = w.unit("TestUpgradeCarrier", 'A', 300, 300);
        Object *near = w.unit("TestUpgradeRecipient", 'A', 305, 300);
        Object *far = w.unit("TestUpgradeRecipient", 'A', 350, 300);
        Object *enemy = w.unit("TestUpgradeRecipient", 'B', 302, 300);
        REQUIRE(carrier);
        REQUIRE(near);
        REQUIRE(far);
        REQUIRE(enemy);
        const UpgradeTemplate *upgrade = TheUpgradeCenter->findUpgrade("Upgrade_Barracks2");
        REQUIRE(upgrade);
        carrier->giveUpgrade(upgrade);
        w.logic->partition().update();
        w.frames(20);
        CHECK(near->hasUpgrade(upgrade));
        CHECK_FALSE(far->hasUpgrade(upgrade));
        CHECK_FALSE(enemy->hasUpgrade(upgrade));
        CHECK_FALSE(near->testStatus(0x42));
    }
}

TEST_CASE("modgap2 give upgrade: linked recipient reports unsupported provider")
{
    for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 }) {
        GiveWorld w;
        w.logic->setEdition(edition);
        CAPTURE(gameEditionName(edition));
        Object *carrier = w.unit("TestUpgradeCarrier", 'A', 300, 300);
        Object *recipient = w.unit("TestUpgradeRecipient", 'A', 305, 300);
        Object *horde = w.unit("TestUpgradeHorde", 'A', 320, 300);
        REQUIRE(carrier);
        REQUIRE(recipient);
        REQUIRE(horde);
        recipient->setProducer(horde);
        const UpgradeTemplate *upgrade = TheUpgradeCenter->findUpgrade("Upgrade_Barracks2");
        REQUIRE(upgrade);
        carrier->giveUpgrade(upgrade);
        auto *ability = dynamic_cast<SpecialAbilityUpdate *>(carrier->findModule("GiveUpgradeUpdate"));
        REQUIRE(ability);
        ability->initiateIntentToDoSpecialPower(ability->abilityData()->m_specialPowerTemplate, recipient, nullptr, 0, 0);
        w.frames(20);
        CHECK_FALSE(recipient->hasUpgrade(upgrade));
        bool stopped = false;
        for (const auto &line : w.logic->report().stops) stopped = stopped || line == GiveUpgradeUpdate::stopLine();
        CHECK_FALSE(stopped); // PORT-199: the producer-linked path is ported (the upgrade goes to the producer's horde), no S-6011 line
    }
}

TEST_CASE("modgap2 give upgrade: a horde receives the upgrade and passes it to eligible members")
{
    for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 }) {
        GiveWorld w;
        w.logic->setEdition(edition);
        CAPTURE(gameEditionName(edition));
        Object *carrier = w.unit("TestUpgradeCarrier", 'A', 300, 300);
        Object *horde = w.unit("TestUpgradeHorde", 'A', 305, 300);
        Object *member = w.unit("TestUpgradeRecipient", 'A', 310, 300);
        REQUIRE(carrier);
        REQUIRE(horde);
        REQUIRE(member);
        REQUIRE(horde->getContain());
        REQUIRE(horde->getContain()->addToContain(member));
        const UpgradeTemplate *upgrade = TheUpgradeCenter->findUpgrade("Upgrade_Barracks2");
        REQUIRE(upgrade);
        REQUIRE(horde->affectedByUpgrade(upgrade));
        carrier->giveUpgrade(upgrade);
        auto *ability = dynamic_cast<SpecialAbilityUpdate *>(carrier->findModule("GiveUpgradeUpdate"));
        REQUIRE(ability);
        ability->initiateIntentToDoSpecialPower(ability->abilityData()->m_specialPowerTemplate, horde, nullptr, 0, 0);
        w.frames(20);
        CHECK(horde->hasUpgrade(upgrade));
        CHECK(member->hasUpgrade(upgrade));
    }
}
