.class Lcom/exo/launcher/InjectThread;
.super Ljava/lang/Thread;
.field final synthetic this$0:Lcom/exo/launcher/MainActivity;
.method constructor <init>(Lcom/exo/launcher/MainActivity;)V
    .registers 2
    invoke-direct {p0}, Ljava/lang/Thread;-><init>()V
    iput-object p1, p0, Lcom/exo/launcher/InjectThread;->this$0:Lcom/exo/launcher/MainActivity;
    return-void
.end method
.method private exec(Ljava/lang/String;)V
    .registers 7
    const/4 v0, 0x3
    new-array v0, v0, [Ljava/lang/String;
    const-string v1, "su"
    const/4 v2, 0x0
    aput-object v1, v0, v2
    const-string v1, "-c"
    const/4 v2, 0x1
    aput-object v1, v0, v2
    const/4 v2, 0x2
    aput-object p1, v0, v2
    invoke-static {}, Ljava/lang/Runtime;->getRuntime()Ljava/lang/Runtime;
    move-result-object v1
    invoke-virtual {v1, v0}, Ljava/lang/Runtime;->exec([Ljava/lang/String;)Ljava/lang/Process;
    move-result-object v1
    invoke-virtual {v1}, Ljava/lang/Process;->waitFor()I
    return-void
.end method
.method public run()V
    .registers 5
    iget-object v0, p0, Lcom/exo/launcher/InjectThread;->this$0:Lcom/exo/launcher/MainActivity;
    const-string v1, "Launching Standoff 2..."
    invoke-virtual {v0, v1}, Lcom/exo/launcher/MainActivity;->setStatus(Ljava/lang/String;)V
    const-string v1, "am start -n com.axlebolt.standoff2/com.unity3d.player.UnityPlayerActivity"
    invoke-direct {p0, v1}, Lcom/exo/launcher/InjectThread;->exec(Ljava/lang/String;)V
    iget-object v0, p0, Lcom/exo/launcher/InjectThread;->this$0:Lcom/exo/launcher/MainActivity;
    const-string v1, "Waiting 8s..."
    invoke-virtual {v0, v1}, Lcom/exo/launcher/MainActivity;->setStatus(Ljava/lang/String;)V
    const-wide/16 v2, 0x1F40
    invoke-static {v2, v3}, Ljava/lang/Thread;->sleep(J)V
    iget-object v0, p0, Lcom/exo/launcher/InjectThread;->this$0:Lcom/exo/launcher/MainActivity;
    const-string v1, "Injecting..."
    invoke-virtual {v0, v1}, Lcom/exo/launcher/MainActivity;->setStatus(Ljava/lang/String;)V
    const-string v1, "/data/local/tmp/exodium_injector com.axlebolt.standoff2 /data/local/tmp/libdvm.so"
    invoke-direct {p0, v1}, Lcom/exo/launcher/InjectThread;->exec(Ljava/lang/String;)V
    iget-object v0, p0, Lcom/exo/launcher/InjectThread;->this$0:Lcom/exo/launcher/MainActivity;
    const-string v1, "Done! Menu in 40s"
    invoke-virtual {v0, v1}, Lcom/exo/launcher/MainActivity;->setStatus(Ljava/lang/String;)V
    return-void
.end method
