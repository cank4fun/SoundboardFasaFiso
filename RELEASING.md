# Creating a release

The project version is defined once in `CMakeLists.txt` and is used for the console banner and Windows executable metadata.

Before publishing:

```powershell
git status
```

The working tree should be clean. Then push the branch and create a tag that matches the project version:

```powershell
git push
git tag v1.0.0
git push origin v1.0.0
```

The **Windows Build and Release** workflow will:

1. Build with MSVC and warnings treated as errors.
2. Create and inspect the portable ZIP.
3. Upload the ZIP as a workflow artifact.
4. Publish the same ZIP on the tagged GitHub Release.

Do not move or reuse an existing release tag. Increment the version in `CMakeLists.txt`, commit it, and create a new matching tag.
