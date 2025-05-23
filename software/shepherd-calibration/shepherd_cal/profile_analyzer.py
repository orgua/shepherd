from pathlib import Path

import pandas as pd

from .profile_cape import ProfileCape


def analyze_directory(
    folder_path: Path,
    stats_path: Path | None = None,
    *,
    do_plots: bool = False,
) -> None:
    stats_list = []
    stat_names = []
    if stats_path is None:
        stats_path = Path("./profile_analysis.csv")
    if not isinstance(folder_path, Path):
        folder_path = Path(folder_path)
    if not isinstance(stats_path, Path):
        stats_path = Path(stats_path)
    if Path(stats_path).exists():
        stats_base = pd.read_csv(stats_path, sep=";", decimal=",", index_col=False)
        stats_list.append(stats_base)
        if "origin" in stats_base.columns:
            stat_names = stats_base["origin"].tolist()

    files: list[Path] = []
    if folder_path.is_file():
        files.append(folder_path)
    elif folder_path.is_dir():
        files = files + list(folder_path.iterdir())
    else:
        msg = f"Provided Path is neither directory or file ({folder_path.as_posix()})"
        raise ValueError(msg)

    for fpath in files:
        if not fpath.is_file():
            continue
        if "npz" not in fpath.suffix.lower():
            continue
        if fpath.stem in stat_names:
            continue

        profile = ProfileCape(fpath)
        stats_list.append(profile.get_stats())

        if do_plots:
            for component in profile.data:
                for filtered in [True, False]:
                    profile.quiver_setpoints_offset(component, filtered=filtered)
                    # profile.scatter_setpoints_stddev(component, filtered=filtered)
                    # profile.scatter_setpoints_dynamic(component, filtered=filtered)

    stat_df = pd.concat(stats_list, axis=0)
    stat_df.to_csv(stats_path, sep=";", decimal=",", index=False)
