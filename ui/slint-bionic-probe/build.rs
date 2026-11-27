fn main() {
    let config = slint_build::CompilerConfiguration::new()
        .embed_resources(slint_build::EmbedResourcesKind::EmbedForSoftwareRenderer);
    slint_build::compile_with_config("../slint-shell/ui/main.slint", config)
        .expect("Slint UI compilation failed");
}
