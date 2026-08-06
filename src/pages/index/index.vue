<template>
    <scroller class="container" scroll-direction="vertical" :show-scrollbar="true">
        <div class="section">
            <text class="section-title">Bilibili</text>
            <text class="item-text">Hello X6PRO</text>
        </div>
    </scroller>
</template>

<style scoped>
.container {
    flex: 1;
    background-color: #fb7299;
}

.section {
    padding: 20px;
}

.section-title {
    font-size: 36px;
    color: #ffffff;
    font-weight: bold;
}

.item-text {
    margin-top: 16px;
    font-size: 28px;
    color: #ffffff;
}
</style>
