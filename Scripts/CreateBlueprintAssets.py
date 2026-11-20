# Run from Unreal Editor's Python script menu after building GameJamEditor.
# 在 UE 中编译 GameJamEditor 后，从“工具 -> 执行 Python 脚本”运行此文件。
# 本脚本负责创建和配置资源，运行时场景由 GameMode 与 GridManager 生成。
import unreal as ue

ROOT = '/Game/OneStep'
TOOL = ue.AssetToolsHelpers.get_asset_tools()
ASSETS = ue.EditorAssetLibrary

def path(name, folder=''):
    return f'{ROOT}/{folder}/{name}' if folder else f'{ROOT}/{name}'

def asset(name, folder, cls, factory):
    # 优先复用同路径资源，使重复运行时保留已有的关卡和材质编辑结果。
    existing = ASSETS.load_asset(path(name, folder)) if ASSETS.does_asset_exist(path(name, folder)) else None
    if existing:
        return existing
    created = TOOL.create_asset(name, f'{ROOT}/{folder}' if folder else ROOT, cls, factory)
    if not created:
        raise RuntimeError(f'Could not create {name}')
    return created

def blueprint(name, parent):
    # 蓝图继承项目中的 C++ 类，默认对象用于写入类级别的可编辑配置。
    factory = ue.BlueprintFactory()
    factory.set_editor_property('parent_class', parent)
    bp = asset(name, 'Blueprints', ue.Blueprint, factory)
    return bp, ue.get_default_object(bp.generated_class())

def save(obj):
    # 保存失败时立即报错，便于从 UE 输出日志定位未落盘的资源。
    if not ASSETS.save_loaded_asset(obj, False):
        raise RuntimeError(f'Could not save {obj.get_path_name()}')


# V6 资源保留原关卡内容，并把关卡组装为带子区域的五个章节。
LEVEL_FIELDS = (
    'title', 'hint', 'rows', 'fragile_lifetime', 'bridges', 'print_effects',
    'require_borrowed_prints', 'challenge', 'prototype', 'turn_controls', 'turn_pivot',
    'initial_deck', 'alternate_deck', 'record_start', 'replay_start', 'replay_tiles',
    'swap_tiles', 'ice_tiles', 'crates', 'rhythm_tiles', 'rhythm_period',
    'rhythm_closed_phase', 'patrol_route', 'legacy_chapter_index',
)


def persist_level(default, name, folder):
    existed = ASSETS.does_asset_exist(path(name, folder))
    factory = ue.DataAssetFactory()
    factory.set_editor_property('data_asset_class', ue.GridLevelDataAsset)
    level = asset(name, folder, ue.GridLevelDataAsset, factory)
    if not existed:
        for field in LEVEL_FIELDS:
            level.set_editor_property(field, default.get_editor_property(field))
        save(level)
    return level

def material():
    # Tint 控制基础色，Glow 控制自发光，C++ 为各组件生成独立动态材质实例。
    mat = asset('M_Stone', 'Art', ue.Material, ue.MaterialFactoryNew())
    lib = ue.MaterialEditingLibrary
    if lib.get_num_material_expressions(mat) > 0:
        return mat
    color = lib.create_material_expression(mat, ue.MaterialExpressionVectorParameter, -420, -130)
    color.set_editor_property('parameter_name', 'Tint')
    color.set_editor_property('default_value', ue.LinearColor(0.13, 0.24, 0.32))
    glow = lib.create_material_expression(mat, ue.MaterialExpressionScalarParameter, -420, 110)
    glow.set_editor_property('parameter_name', 'Glow')
    glow.set_editor_property('default_value', 0.08)
    mul = lib.create_material_expression(mat, ue.MaterialExpressionMultiply, -160, 135)
    rough = lib.create_material_expression(mat, ue.MaterialExpressionConstant, -180, -100)
    rough.set_editor_property('r', 0.84)
    lib.connect_material_expressions(color, '', mul, 'A')
    lib.connect_material_expressions(glow, '', mul, 'B')
    lib.connect_material_property(rough, '', ue.MaterialProperty.MP_ROUGHNESS)
    lib.connect_material_property(color, '', ue.MaterialProperty.MP_BASE_COLOR)
    lib.connect_material_property(mul, '', ue.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.recompile_material(mat)
    save(mat)
    return mat

def main():
    palette = material()
    levels = []
    for i, default in enumerate(ue.GridManager.create_default_levels(), 1):
        # 普通关卡使用 V4 目录，带转桥、录制或换位的关卡使用 V5 目录。
        factory = ue.DataAssetFactory()
        factory.set_editor_property('data_asset_class', ue.GridLevelDataAsset)
        version = 'V4' if default.get_editor_property('prototype') == ue.GridPrototype.NONE else 'V5'
        name = f'DA_{version}_Chapter{i:02}'
        folder = f'Levels/{version}'
        existed = ASSETS.does_asset_exist(path(name, folder))
        level = asset(name, folder, ue.GridLevelDataAsset, factory)
        if not existed:
            # 新建关卡从 C++ 默认定义复制字段，已存在的关卡保留资源内的配置。
            for field in ('title', 'hint', 'rows', 'fragile_lifetime', 'bridges', 'print_effects',
                          'require_borrowed_prints', 'challenge', 'prototype', 'turn_controls', 'turn_pivot',
                          'initial_deck', 'alternate_deck', 'record_start', 'replay_start', 'replay_tiles', 'swap_tiles'):
                level.set_editor_property(field, default.get_editor_property(field))
            save(level)
        levels.append(level)

    # 独立实验使用单独资源，之后组合到长章时仍可单独试玩。
    for i, default in enumerate(ue.GridManager.create_fusion_levels(), 1):
        persist_level(default, f'DA_V6_Experiment{i:02}', 'Levels/V6/Experiments')
    chapters = []
    for i, default in enumerate(ue.GridManager.create_campaign_levels(levels), 1):
        rooms = []
        for j, section in enumerate(default.get_editor_property('sections'), 1):
            rooms.append(persist_level(section, f'DA_V6_Room{i:02}_{j:02}', 'Levels/V6/Rooms'))
        chapter = persist_level(default, f'DA_V6_Chapter{i:02}', 'Levels/V6')
        chapter.set_editor_property('sections', rooms)
        save(chapter)
        chapters.append(chapter)
    if len(chapters) != 5:
        raise RuntimeError('Expected five chapters before updating BP_Board')

    tile, tile_defaults = blueprint('BP_StoneTile', ue.GridTile)
    tile_defaults.set_editor_property('memory_stone', ue.LinearColor(0.24, 0.36, 0.40))
    tile_defaults.set_editor_property('normal_stone', ue.LinearColor(0.13, 0.22, 0.29))
    save(tile)

    character, character_defaults = blueprint('BP_Traveler', ue.GridPuzzleCharacter)
    character_defaults.set_editor_property('scarf_color', ue.LinearColor(0.82, 0.78, 0.61))
    save(character)

    # 管理器蓝图把关卡数组、地块类、人物类与调色材质连接起来。
    manager, manager_defaults = blueprint('BP_Board', ue.GridManager)
    manager_defaults.set_editor_property('levels', chapters)
    manager_defaults.set_editor_property('tile_class', tile.generated_class())
    manager_defaults.set_editor_property('character_class', character.generated_class())
    manager_defaults.set_editor_property('palette_material', palette)
    save(manager)

    # DefaultEngine.ini 指向这个 GameMode，运行时通过它生成 BP_Board。
    game, game_defaults = blueprint('BP_OneStepGame', ue.GridPuzzleGameMode)
    game_defaults.set_editor_property('manager_class', manager.generated_class())
    save(game)

    ue.log('ONE_STEP_ASSETS_READY')

main()
