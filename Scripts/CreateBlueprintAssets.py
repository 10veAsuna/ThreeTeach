# Run from Unreal Editor's Python script menu after building GameJamEditor.
import unreal as ue

ROOT = '/Game/OneStep'
TOOL = ue.AssetToolsHelpers.get_asset_tools()
ASSETS = ue.EditorAssetLibrary

def path(name, folder=''):
    return f'{ROOT}/{folder}/{name}' if folder else f'{ROOT}/{name}'

def asset(name, folder, cls, factory):
    existing = ASSETS.load_asset(path(name, folder)) if ASSETS.does_asset_exist(path(name, folder)) else None
    if existing:
        return existing
    created = TOOL.create_asset(name, f'{ROOT}/{folder}' if folder else ROOT, cls, factory)
    if not created:
        raise RuntimeError(f'Could not create {name}')
    return created

def blueprint(name, parent):
    factory = ue.BlueprintFactory()
    factory.set_editor_property('parent_class', parent)
    bp = asset(name, 'Blueprints', ue.Blueprint, factory)
    return bp, ue.get_default_object(bp.generated_class())

def save(obj):
    if not ASSETS.save_loaded_asset(obj, False):
        raise RuntimeError(f'Could not save {obj.get_path_name()}')

def material():
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
        factory = ue.DataAssetFactory()
        factory.set_editor_property('data_asset_class', ue.GridLevelDataAsset)
        version = 'V4' if default.get_editor_property('prototype') == ue.GridPrototype.NONE else 'V5'
        name = f'DA_{version}_Chapter{i:02}'
        folder = f'Levels/{version}'
        existed = ASSETS.does_asset_exist(path(name, folder))
        level = asset(name, folder, ue.GridLevelDataAsset, factory)
        if not existed:
            for field in ('title', 'hint', 'rows', 'fragile_lifetime', 'bridges', 'print_effects',
                          'require_borrowed_prints', 'challenge', 'prototype', 'turn_controls', 'turn_pivot',
                          'initial_deck', 'alternate_deck', 'record_start', 'replay_start', 'replay_tiles', 'swap_tiles'):
                level.set_editor_property(field, default.get_editor_property(field))
            save(level)
        levels.append(level)

    tile, tile_defaults = blueprint('BP_StoneTile', ue.GridTile)
    tile_defaults.set_editor_property('memory_stone', ue.LinearColor(0.24, 0.36, 0.40))
    tile_defaults.set_editor_property('normal_stone', ue.LinearColor(0.13, 0.22, 0.29))
    save(tile)

    character, character_defaults = blueprint('BP_Traveler', ue.GridPuzzleCharacter)
    character_defaults.set_editor_property('scarf_color', ue.LinearColor(0.82, 0.78, 0.61))
    save(character)

    manager, manager_defaults = blueprint('BP_Board', ue.GridManager)
    manager_defaults.set_editor_property('levels', levels)
    manager_defaults.set_editor_property('tile_class', tile.generated_class())
    manager_defaults.set_editor_property('character_class', character.generated_class())
    manager_defaults.set_editor_property('palette_material', palette)
    save(manager)

    game, game_defaults = blueprint('BP_OneStepGame', ue.GridPuzzleGameMode)
    game_defaults.set_editor_property('manager_class', manager.generated_class())
    save(game)

    ue.log('ONE_STEP_ASSETS_READY')

main()
