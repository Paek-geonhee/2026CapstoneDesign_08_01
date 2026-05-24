import unreal


def import_texture(file_path, destination):

    task = unreal.AssetImportTask()

    task.filename = file_path
    task.destination_path = destination
    task.automated = True
    task.save = True

    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])